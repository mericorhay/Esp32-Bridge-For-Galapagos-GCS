#include "bridge/uart_radio.hpp"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "uart";

namespace bridge {

// UART2 is unused on every common ESP32 devkit and has its own GPIO
// routing, so it's the natural home for the radio. UART0 is the debug
// console; stealing it would make `idf.py monitor` garbage.
static constexpr uart_port_t kUartNum = UART_NUM_2;
static constexpr size_t kRxBufBytes = 2048;   // radio-to-ESP32 (downlink)
static constexpr size_t kTxBufBytes = 2048;   // ESP32-to-radio (uplink)
static constexpr size_t kEventQueueDepth = 16;

bool UartRadio::init(uint32_t baud, uint8_t tx_pin, uint8_t rx_pin) noexcept {
    // Every field, in declaration order: leaving one out trips
    // -Wmissing-field-initializers (turned fatal by ESP-IDF), and an
    // unnamed struct field like `flags` is the easiest to forget.
    const uart_config_t cfg = {
        .baud_rate = static_cast<int>(baud),
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {.allow_pd = 0, .backup_before_sleep = 0},
    };
    if (uart_driver_install(kUartNum, kRxBufBytes, kTxBufBytes,
                            kEventQueueDepth, nullptr, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart driver install failed");
        return false;
    }
    if (uart_param_config(kUartNum, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "uart param config failed");
        return false;
    }
    if (uart_set_pin(kUartNum, tx_pin, rx_pin, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "uart pin config failed");
        return false;
    }
    ESP_LOGI(TAG, "UART2 up at %lu baud (tx=%u rx=%u)", (unsigned long)baud,
             tx_pin, rx_pin);
    return true;
}

size_t UartRadio::read(std::span<uint8_t> out, uint32_t timeout_ms) noexcept {
    int n = uart_read_bytes(kUartNum, out.data(), out.size(),
                            pdMS_TO_TICKS(timeout_ms));
    return n > 0 ? static_cast<size_t>(n) : 0;
}

size_t UartRadio::write(std::span<const uint8_t> bytes) noexcept {
    int n = uart_write_bytes(kUartNum, bytes.data(), bytes.size());
    return n > 0 ? static_cast<size_t>(n) : 0;
}

}  // namespace bridge
