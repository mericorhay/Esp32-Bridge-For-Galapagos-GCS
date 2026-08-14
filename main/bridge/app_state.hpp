#pragma once

// Shared pipeline state and the four relay tasks.
//
// Wiring is deliberately boring:
//   uart_rx_task:  UART -> g_rx_ring, kick g_udp_tx_task
//   udp_tx_task:   g_rx_ring -> UDP broadcast
//   udp_rx_task:   UDP recv -> g_tx_ring, kick g_uart_tx_task
//   uart_tx_task:  g_tx_ring -> UART
//
// One thread owns each ring endpoint; the rings are lock-free SPSC (see
// spsc_ring.hpp). Notifications use task notifications (semaphore-ish),
// not polling — the consumer sleeps until its producer has work.

#include <atomic>
#include <cstdint>

#include "bridge/spsc_ring.hpp"
#include "bridge/state_machine.hpp"
#include "bridge/udp_relay.hpp"

namespace bridge {

// Ring sizes are powers of two and generous for the wire: SiK at 57600
// baud pushes ~5.7 KB/s, LoRa far less. These sizes only matter when the
// UDP link is congested — the bridge drops and counts rather than block.
constexpr size_t kRxRingCapacity = 4096;  // vehicle -> app
constexpr size_t kTxRingCapacity = 1024;  // app -> vehicle

extern SpScRing<kRxRingCapacity> g_rx_ring;
extern SpScRing<kTxRingCapacity> g_tx_ring;
extern UdpRelay g_relay;
extern StateMachine g_sm;

// Microsecond timestamp (esp_timer) of the last byte arrival on the radio
// UART, written by uart_rx_task and read by link_watch_task. This is the
// *arrival* signal, not "is the ring currently non-empty" — the consumer
// drains the ring quickly, so a ring-empty check would otherwise let the
// watchdog think the radio went silent while bytes are still streaming.
extern std::atomic<int64_t> g_last_uart_rx_us;

// Bring up the pipeline: init the UART (at `baud`) and UDP relay, create
// the four relay tasks plus the watchdog/LED tasks. Call after WiFi is up
// and the state machine is in WifiConnected. Returns false if a driver
// refused to start (caller marks the machine Fatal and gives up).
bool start_pipeline(uint32_t baud);

void uart_rx_task(void* arg);
void udp_tx_task(void* arg);
void udp_rx_task(void* arg);
void uart_tx_task(void* arg);
void link_watch_task(void* arg);
void led_task(void* arg);

}  // namespace bridge
