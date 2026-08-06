#pragma once

// Bridge configuration. Compile-time defaults live here; the values a
// pilot actually changes (WiFi name/password, radio baud) are read from
// NVS at boot and override them — see config.cpp.
//
// Pins are chip-specific: ESP32/S3 use UART2 on GPIO16/17, ESP32-C3
// uses UART1 on GPIO6/7 (C3 has no UART2). LED pin also varies.

#include <cstdint>

namespace bridge {

constexpr uint32_t kUdpPort = 14550;   // Galapagos listens here by default
constexpr uint32_t kDefaultBaud = 57600;  // SiK radio default

// --- Chip-specific UART and pin config ---
#if defined(TARGET_ESP32C3)
  // ESP32-C3: only UART0 + UART1. Use UART1 for the radio.
  // GPIO6 (TX) and GPIO7 (RX) are safe general-purpose pins with no
  // boot-mode or flash-mux conflicts.
  constexpr uint8_t kUartTxPin = 6;
  constexpr uint8_t kUartRxPin = 7;
  constexpr uint8_t kLedPin = 12;  // GPIO12 — safe on C3 DevKit
#else
  // ESP32 / ESP32-S3: UART2 on GPIO16/17 (classic DevKitC layout).
  constexpr uint8_t kUartTxPin = 17;  // GPIO17 -> radio RX
  constexpr uint8_t kUartRxPin = 16;  // GPIO16 <- radio TX
  constexpr uint8_t kLedPin = 2;      // onboard blue LED
#endif

struct BridgeConfig {
    char ssid[33];
    char password[65];
    uint8_t channel;
    uint32_t baud;
};

// Defaults, as a constexpr instance.
constexpr BridgeConfig kDefaultConfig = {
    "Galapagos-Bridge",
    "galapagos",
    1,
    57600,
};

// NVS-backed copy: fills `out` from flash, falling back to kDefaultConfig
// for any key that was never written. Returns true if NVS was at least
// usable (so the caller can log a meaningful error instead of guessing).
bool load_config(BridgeConfig& out);

}  // namespace bridge
