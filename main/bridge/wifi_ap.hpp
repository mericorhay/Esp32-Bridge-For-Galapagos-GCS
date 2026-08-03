#pragma once

// SoftAP bring-up. The bridge IS the network — there is no router in the
// field, so the ESP32 advertises its own SSID and hands the pilot's device
// a 192.168.4.x address. This is also the entire attack surface the
// firmware has, so the SSID/password come from config (NVS-overridable)
// and nothing else is exposed.

#include "bridge/config.hpp"

namespace bridge {

// Start the AP. Blocking until `esp_wifi_start` returns. Returns false if
// any step failed (nearly always: NVS partition not flashed, which the
// build already covers for `idf.py flash`).
bool wifi_ap_start(const BridgeConfig& cfg) noexcept;

}  // namespace bridge
