#pragma once

// One UDP socket, both directions.
//
// Galapagos binds 0.0.0.0:14550 and *listens* by default (its "WiFi" mode
// is `udp://` = "bridge reaches me"). So the bridge pushes telemetry as a
// broadcast to port 14550 and the app picks it up — no IP to configure,
// which is the whole point of the zero-config story. The socket keeps
// source port 14550, so when the app does answer a command it replies to
// 192.168.4.1:14550 and lands right back on this same socket, where it is
// forwarded to the radio over UART.
//
// lwIP recvfrom/sendto are thread-safe on separate calls but not on the
// same socket from two tasks concurrently, so both directions are driven
// from a single udp_rx_task (see app_state.cpp).

#include <cstddef>
#include <cstdint>
#include <span>

namespace bridge {

class UdpRelay {
  public:
    bool init(uint16_t port) noexcept;

    // Broadcast `bytes` to <broadcast>:port. Returns true if queued.
    bool send_broadcast(std::span<const uint8_t> bytes) noexcept;

    // Unicast `bytes` to a specific IPv4:port (a GCS whose address was
    // learned from an inbound datagram). Returns true if queued.
    bool send_unicast_to(uint32_t ipv4, uint16_t port,
                         std::span<const uint8_t> bytes) noexcept;

    // Block up to `timeout_ms` for an incoming datagram into `out`. Returns
    // bytes received (0 == timeout). On return, `peer_ip` holds the sender's
    // IPv4 (network byte order) — used to learn the GCS address from its
    // first command, no WiFi-event plumbing required.
    size_t recv(std::span<uint8_t> out, uint32_t timeout_ms,
                uint32_t* peer_ip) noexcept;

    int fd() const noexcept { return fd_; }

  private:
    int fd_ = -1;
};

}  // namespace bridge
