#pragma once

// Benchmark/demo mode: a virtual ArduPilot Copter that speaks real MAVLink
// frames, so Galapagos can be exercised button-by-button with NO radio on
// the bench — the ESP32 simply broadcasts synthetic telemetry and answers
// commands, exactly like the Rust SITL tool does on a desktop.
//
// This is *not* part of the production data plane. It is a separate task
// (see demo_task) that owns its own UDP socket and never touches the relay
// rings, so enabling it cannot disturb a real link. It exists because a
// radio-less bridge otherwise broadcasts silence — and a GCS stuck on
// "Connecting…" proves nothing.

#include <cstdint>
#include <span>

#include "bridge/udp_relay.hpp"

namespace bridge {

// True: run the demo instead of relying on the radio. False: pure relay.
// Hard-coded at build time so there is no runtime path that could silently
// feed synthetic data into a real flight.
constexpr bool kDemoMode = false;

// Broadcast one MAVLink v2 frame on the demo socket.
void demo_send_frame(UdpRelay& relay, uint32_t msgid, std::span<const uint8_t> payload);

// Remember the GCS's IP (learned from WIFI_EVENT_AP_STAIPASSIGNED) so the
// demo can unicast telemetry to it. iOS is more reliable about *unicast*
// than *broadcast* on a local network, so once a client connects we send
// both ways.
void demo_set_peer(uint32_t ipv4);

// The demo loop. Runs forever; owned by a FreeRTOS task created in
// start_pipeline when kDemoMode is set.
void demo_task(void* arg);

}  // namespace bridge
