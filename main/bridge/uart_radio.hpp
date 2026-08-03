#pragma once

// Thin wrapper over the ESP-IDF UART driver for UART2, the radio's wire.
//
// The driver's own internal ring buffer absorbs the ISR->task hop (that's
// what `uart_driver_install` with a queue is for — reimplementing it by
// hand would be cargo-culting). What this wrapper adds is a stable,
// testable interface and the two "one of these three things went wrong"
// failure modes the rest of the pipeline can actually act on.

#include <cstddef>
#include <cstdint>
#include <span>

namespace bridge {

class UartRadio {
  public:
    // Install the driver. `baud` is the radio's rate (SiK defaults to
    // 57600, LoRa links often 9600). Returns false if the driver install
    // or pin assignment failed — the only real failure at this layer.
    bool init(uint32_t baud, uint8_t tx_pin, uint8_t rx_pin) noexcept;

    // Block up to `timeout_ms` for data, then return whatever arrived in
    // `out`. Returns bytes read (0 == timeout, no error).
    size_t read(std::span<uint8_t> out, uint32_t timeout_ms) noexcept;

    // Write `n` bytes. Returns bytes actually queued to the driver.
    size_t write(std::span<const uint8_t> bytes) noexcept;
};

}  // namespace bridge
