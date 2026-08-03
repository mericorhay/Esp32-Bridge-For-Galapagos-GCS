#pragma once

// Bridge configuration. Compile-time defaults live here; the values a
// pilot actually changes (WiFi name/password, radio baud) are read from
// NVS at boot and override them — see config.cpp.
//
// Pins default to the classic ESP32 DevKitC layout (GPIO16/17 = UART2)
// because that's the board 95% of pilots own. Changing pins means
// reflashing; changing baud/SSID/password does not.

#include <cstdint>

namespace bridge {

constexpr uint32_t kUdpPort = 14550;   // Galapagos listens here by default
constexpr uint32_t kDefaultBaud = 57600;  // SiK radio default
constexpr uint8_t kUartTxPin = 17;        // GPIO17 -> radio RX
constexpr uint8_t kUartRxPin = 16;        // GPIO16 <- radio TX
constexpr uint8_t kLedPin = 2;            // DevKitC onboard blue LED

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
