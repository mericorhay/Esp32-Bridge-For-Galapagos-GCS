#include "bridge/uart_radio.hpp"

#include <cstring>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "uart";

namespace bridge {

// ESP32-C3 has only UART0 + UART1 (no UART2). ESP32/S3 use UART2
// to keep the debug console (UART0) free.
#if defined(TARGET_ESP32C3)
static constexpr uart_port_t kUartNum = UART_NUM_1;
#else
static constexpr uart_port_t kUartNum = UART_NUM_2;
#endif

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
        uart_driver_delete(kUartNum);  // release the driver installed above
        return false;
    }
    if (uart_set_pin(kUartNum, tx_pin, rx_pin, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "uart pin config failed");
        uart_driver_delete(kUartNum);
        return false;
    }
    baud_ = baud;
#if defined(TARGET_ESP32C3)
    ESP_LOGI(TAG, "UART1 up at %lu baud (tx=%u rx=%u)", (unsigned long)baud,
             tx_pin, rx_pin);
#else
    ESP_LOGI(TAG, "UART2 up at %lu baud (tx=%u rx=%u)", (unsigned long)baud,
             tx_pin, rx_pin);
#endif
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

// ---------------------------------------------------------------------------
// Autobaud: find the radio's real rate by listening on the usual suspects.
// ---------------------------------------------------------------------------

// Rates a telemetry radio is likely to speak. SiK/3DR default to 57600,
// generic telemetry links often 115200, ELRS MAVLink mode uses 460800,
// and classic LoRa links sit at 9600.
static constexpr uint32_t kBaudCandidates[] = {
    57600, 115200, 460800, 38400, 230400, 19200, 9600,
};

// How long to listen on one baud before moving to the next. A radio
// pushing telemetry at 4 Hz delivers a frame every 250 ms, so 800 ms
// gives at least a couple of chances to lock on.
static constexpr uint32_t kScanWindowMs = 800;

// True when `buf[0..n)` contains two consecutive, well-formed MAVLink
// frames (v1 or v2). Cheap and robust: find an STX byte, read the
// claimed payload length, and check that another STX sits at exactly
// `header + payload + 2` bytes (CRC) later. Two in a row makes a false
// positive from link noise vanishingly unlikely.
static bool looks_like_mavlink_stream(const uint8_t* buf, size_t n) {
    for (size_t i = 0; i + 1 < n; ++i) {
        const bool v2 = buf[i] == 0xFD;
        if (!v2 && buf[i] != 0xFE) continue;
        const size_t header = v2 ? 10 : 6;
        if (i + header >= n) break;  // not enough data for the payload len byte
        const size_t payload = buf[i + 1];
        const size_t frame_size = header + payload + 2;
        if (frame_size > 255 + header) break;      // reject absurd claims
        if (i + frame_size >= n) break;            // frame incomplete
        const uint8_t next = buf[i + frame_size];
        if (next == 0xFD || next == 0xFE) return true;
    }
    return false;
}

bool UartRadio::autobaud_scan(uint32_t configured_baud) noexcept {
    // The configured rate goes first: pilots who set the right baud get
    // an instant lock instead of waiting out the scan window.
    uint32_t ordered[sizeof kBaudCandidates / sizeof kBaudCandidates[0] + 1];
    size_t count = 0;
    if (configured_baud != 0) ordered[count++] = configured_baud;
    for (uint32_t b : kBaudCandidates) {
        bool dup = false;
        for (size_t j = 0; j < count; ++j) {
            if (ordered[j] == b) {
                dup = true;
                break;
            }
        }
        if (!dup) ordered[count++] = b;
    }

    uint8_t buf[1024];
    for (size_t i = 0; i < count; ++i) {
        const uint32_t baud = ordered[i];
        if (uart_set_baudrate(kUartNum, baud) != ESP_OK) continue;
        uart_flush_input(kUartNum);

        size_t filled = 0;
        const uint64_t deadline = esp_timer_get_time()
                                  + kScanWindowMs * 1000ULL;
        while (esp_timer_get_time() < deadline) {
            int n = uart_read_bytes(kUartNum, buf + filled,
                                    sizeof(buf) - filled,
                                    pdMS_TO_TICKS(25));
            if (n > 0) filled += static_cast<size_t>(n);
            if (filled >= 12 && looks_like_mavlink_stream(buf, filled)) {
                baud_ = baud;
                ESP_LOGI(TAG, "autobaud locked on %lu baud", (unsigned long)baud);
                return true;
            }
            // Keep only the tail that might hold a partial frame start.
            if (filled == sizeof(buf)) {
                memmove(buf, buf + sizeof(buf) - 16, 16);
                filled = 16;
            }
        }
        ESP_LOGD(TAG, "autobaud: no MAVLink on %lu baud", (unsigned long)baud);
    }

    // Nothing spoke MAVLink; fall back to the configured rate so the
    // bridge still behaves exactly as before for known radios.
    if (configured_baud != 0) uart_set_baudrate(kUartNum, configured_baud);
    baud_ = configured_baud;
    ESP_LOGW(TAG, "autobaud: no MAVLink stream found, keeping %lu baud",
             (unsigned long)configured_baud);
    return false;
}

}  // namespace bridge
