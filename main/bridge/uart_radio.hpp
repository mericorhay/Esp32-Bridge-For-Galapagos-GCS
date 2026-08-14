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

    // Find the radio's real baud rate by scanning the usual suspects
    // (SiK 57600, telemetry 115200, ELRS 460800, LoRa 9600...) and
    // listening for a chain of well-formed MAVLink frames on each. The
    // configured rate is tried first, so a pilot who knows their radio
    // still wins; unknown radios just work. On failure the configured
    // baud stays in effect and `false` is returned (caller logs).
    bool autobaud_scan(uint32_t configured_baud) noexcept;

    // The baud rate currently in effect (configured, or the one the scan
    // locked onto). 0 until init() succeeds.
    uint32_t current_baud() const noexcept { return baud_; }

  private:
    uint32_t baud_ = 0;
};

}  // namespace bridge
