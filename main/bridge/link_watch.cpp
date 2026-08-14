#include "bridge/app_state.hpp"

#include <cstdint>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bridge/state_machine.hpp"

static const char* TAG = "watch";

namespace bridge {

// The bridge never parses MAVLink (that's the app's job, and the whole
// point of the raw relay). It still needs to notice when the radio has
// gone quiet — ESP32's battery died, the SiK link dropped, someone kicked
// the power bank — so it watches the one signal it does understand: bytes
// flowing out of the ring toward the app. kLinkTimeoutSeconds of silence
// flips the state machine to LinkSilent, which the app mirrors with its
// own heartbeat watchdog once it stops receiving anything.
void link_watch_task(void*) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        // Read the arrival timestamp written by uart_rx_task, not the ring's
        // current emptiness — the consumer drains the ring fast, so checking
        // `g_rx_ring.empty()` would read "the ring happens to be empty right
        // now" and could fire RadioSilence while bytes are still streaming.
        int64_t last_rx = g_last_uart_rx_us.load(std::memory_order_acquire);
        if (last_rx == 0) continue;  // no UART traffic yet — still booting
        int64_t silent_us = esp_timer_get_time() - last_rx;
        if (silent_us > static_cast<int64_t>(kLinkTimeoutSeconds) * 1'000'000) {
            if (g_sm.dispatch(Event::RadioSilence)) {
                ESP_LOGW(TAG, "radio silent for %d s", kLinkTimeoutSeconds);
            }
            // Avoid re-firing every 500 ms: reset the clock so the next
            // RadioSilence can only come after another full timeout.
            g_last_uart_rx_us.store(esp_timer_get_time(), std::memory_order_release);
        }
    }
}

}  // namespace bridge
