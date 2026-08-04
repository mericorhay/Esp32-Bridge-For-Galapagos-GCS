#include "bridge/app_state.hpp"

#include <cstdint>
#include <span>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bridge/config.hpp"
#include "bridge/demo.hpp"
#include "bridge/uart_radio.hpp"
#include "bridge/udp_relay.hpp"

static const char* TAG = "pipe";

namespace bridge {

SpScRing<kRxRingCapacity> g_rx_ring;
SpScRing<kTxRingCapacity> g_tx_ring;
UdpRelay g_relay;
StateMachine g_sm;

// Task handles for cross-task notifications.
static TaskHandle_t s_udp_tx_task;
static TaskHandle_t s_uart_tx_task;

static UartRadio s_uart;

bool start_pipeline(uint32_t baud) {
    if (!s_uart.init(baud, kUartTxPin, kUartRxPin)) return false;
    if constexpr (kDemoMode) {
        // Bench mode: the demo task owns its own UDP socket and never
        // touches the relay rings. Bind g_relay anyway would double-bind
        // port 14550 — skip it so there's exactly one socket on the port.
        constexpr UBaseType_t kRelayPriority = 5;
        constexpr UBaseType_t kWatchPriority = 3;
        constexpr UBaseType_t kLedPriority = 1;
        xTaskCreate(demo_task, "demo", 4096, nullptr, kRelayPriority, nullptr);
        xTaskCreate(link_watch_task, "watch", 2048, nullptr, kWatchPriority, nullptr);
        xTaskCreate(led_task, "led", 2048, nullptr, kLedPriority, nullptr);
        return true;
    }

    if (!g_relay.init(kUdpPort)) return false;

    // Priorities: relay tasks share a level and cooperate via notifications
    // (no starve risk); the watchdog and LED sit below them so they never
    // delay a byte. Stacks are small on purpose — each task's 512-byte
    // local buffer is the bulk of its stack, kept as a local array rather
    // than heap so an OOM mid-flight can't happen.
    constexpr UBaseType_t kRelayPriority = 5;
    constexpr UBaseType_t kWatchPriority = 3;
    constexpr UBaseType_t kLedPriority = 1;

    // Create the consumers first: udp_tx_task/uart_tx_task write their
    // own TaskHandle via the last arg, and the producers (uart_rx/udp_rx)
    // call xTaskNotifyGive on it the moment their first byte arrives.
    xTaskCreate(udp_tx_task, "udp_tx", 3072, nullptr, kRelayPriority, &s_udp_tx_task);
    xTaskCreate(uart_tx_task, "uart_tx", 3072, nullptr, kRelayPriority, &s_uart_tx_task);
    xTaskCreate(uart_rx_task, "uart_rx", 3072, nullptr, kRelayPriority, nullptr);
    xTaskCreate(udp_rx_task, "udp_rx", 3072, nullptr, kRelayPriority, nullptr);
    xTaskCreate(link_watch_task, "watch", 2048, nullptr, kWatchPriority, nullptr);
    xTaskCreate(led_task, "led", 2048, nullptr, kLedPriority, nullptr);
    return true;
}

void uart_rx_task(void*) {
    // 512 B covers several MAVLink frames per read without a big stack.
    uint8_t buf[512];
    for (;;) {
        size_t n = s_uart.read(buf, 20);
        if (n == 0) continue;
        size_t stored = g_rx_ring.push(buf, n);
        if (stored < n) {
            ESP_LOGW(TAG, "rx ring full: dropped %u bytes",
                     static_cast<unsigned>(n - stored));
        }
        xTaskNotifyGive(s_udp_tx_task);
    }
}

void udp_tx_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            auto span = g_rx_ring.pop_front();
            if (span.empty()) break;
            if (!g_relay.send_broadcast(span)) {
                // Send failed (e.g. lwIP momentarily out of pbufs). The
                // bytes are still "consumed" — replaying them is worse
                // than dropping, since MAVLink ordering matters more than
                // completeness for a live feed.
                g_rx_ring.consume(span.size());
                continue;
            }
            g_rx_ring.consume(span.size());
            g_sm.dispatch(Event::RadioData);
        }
    }
}

void udp_rx_task(void*) {
    uint8_t buf[512];
    for (;;) {
        uint32_t peer = 0;
        size_t n = g_relay.recv(std::span<uint8_t>(buf, sizeof buf), 50, &peer);
        if (n == 0) continue;
        g_tx_ring.push(buf, n);
        xTaskNotifyGive(s_uart_tx_task);
    }
}

void uart_tx_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            auto span = g_tx_ring.pop_front();
            if (span.empty()) break;
            s_uart.write(span);
            g_tx_ring.consume(span.size());
        }
    }
}

// One physical signal the pilot can read at a glance without opening the
// app: solid = live link, slow blink = radio silent, fast blink = WiFi
// connecting, off = error. Pattern is derived from the state machine, so
// it can never disagree with what the app will report.
void led_task(void*) {
    constexpr uint32_t kPeriodMs = 200;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        const State st = g_sm.current();
        bool on;
        switch (st) {
            case State::LinkUp:       on = true;  break;
            case State::LinkSilent:   on = (xTaskGetTickCount() / 500) % 2 == 0; break;
            case State::WifiConnecting: on = (xTaskGetTickCount() / 200) % 2 == 0; break;
            case State::Error:        on = false; break;
            default:                  on = true;  break;  // boot/wifi up
        }
        gpio_set_level(static_cast<gpio_num_t>(kLedPin), on ? 1 : 0);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kPeriodMs));
    }
}

}  // namespace bridge
