#include "bridge/udp_relay.hpp"

#include <cerrno>
#include <cstring>

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char* TAG = "udp";

namespace bridge {

bool UdpRelay::init(uint16_t port) noexcept {
    fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ < 0) {
        ESP_LOGE(TAG, "socket: %s", strerror(errno));
        return false;
    }

    // Reuse so a crash-and-reboot in the field can rebind immediately
    // instead of hitting EADDRINUSE for a few seconds while old TIME_WAIT
    // lingers (UDP has no TIME_WAIT, but the SO_REUSEADDR habit is cheap).
    int on = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    // The default is broadcast-silent; this is the one socket that needs it.
    int bcast = 1;
    setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind %u: %s", port, strerror(errno));
        return false;
    }

    ESP_LOGI(TAG, "UDP bound on 0.0.0.0:%u (broadcast out)", port);
    return true;
}

bool UdpRelay::send_broadcast(std::span<const uint8_t> bytes) noexcept {
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(14550);
    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    ssize_t n = sendto(fd_, bytes.data(), bytes.size(), 0,
                       reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    if (n < 0) {
        ESP_LOGW(TAG, "sendto: %s", strerror(errno));
        return false;
    }
    return true;
}

size_t UdpRelay::recv(std::span<uint8_t> out, uint32_t timeout_ms) noexcept {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd_, &rfds);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ready = select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
    if (ready <= 0) return 0;
    ssize_t n = recvfrom(fd_, out.data(), out.size(), 0, nullptr, nullptr);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

}  // namespace bridge
