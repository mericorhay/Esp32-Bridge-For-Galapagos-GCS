#pragma once

// Single-producer / single-consumer lock-free byte ring.
//
// The pipeline's two hops are both exactly this shape:
//   uart_rx_task (producer)  -> ring ->  udp_tx_task (consumer)
//   udp_rx_task (producer)   -> ring ->  uart_tx_task (consumer)
// Neither pair ever writes from the other side, which is what lets this be
// lock-free without a CAS: an SPSC ring is just two monotonic indices that
// each side reads/writes with the right memory order. Add a mutex and you
// get correctness for a problem that didn't exist; skip it and you get a
// queue whose producer can run in an ISR with zero context-switch cost.
//
// Ordering contract (the part that actually matters):
//   * producer: writes payload bytes, then `head_` with release.
//   * consumer: reads `head_` with acquire (sees the bytes), consumes.
//   * producer's `push` reads `tail_` with acquire to learn free space —
//     this is what makes a consumer's `consume` (release on `tail_`) visible.
// The two hot indices live on separate cache lines so neither side
// invalidates the other's line every push/pop.
//
// Indices are allowed to grow past SIZE_MAX/2? No — but they *are* allowed
// to wrap naturally; modulo is applied only on the array index, never on
// the head-tail arithmetic, so correctness survives (Capacity-1) full wraps.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace bridge {

template <size_t Capacity>
class SpScRing {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "SpScRing capacity must be a power of two");

  public:
    constexpr SpScRing() = default;

    // Copy up to `n` bytes in. Returns bytes actually stored; anything not
    // stored is counted in `dropped()` and gone forever. The caller (UART
    // RX) has no back-pressure lever against a slow radio — it must never
    // block, so dropping-and-counting is the only honest option.
    size_t push(const uint8_t* data, size_t n) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        const size_t free = Capacity - (head - tail);
        const size_t to_store = n < free ? n : free;
        dropped_.fetch_add(n - to_store, std::memory_order_relaxed);
        for (size_t i = 0; i < to_store; ++i) {
            buf_[(head + i) & (Capacity - 1)] = data[i];
        }
        head_.store(head + to_store, std::memory_order_release);
        return to_store;
    }

    // The contiguous readable region starting at the consumer index.
    // May be shorter than the true occupancy because the ring may have
    // wrapped — the caller sends `span` and then calls consume(span.size()).
    // The span is only valid until the next push/consume, but the consumer
    // is the only side touching it, so between pop_front and consume the
    // producer can at most grow the ring behind the consumer's tail.
    std::span<const uint8_t> pop_front() const noexcept {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t avail = head - tail;
        if (avail == 0) return {};
        const size_t start = tail & (Capacity - 1);
        const size_t n = avail < (Capacity - start) ? avail : (Capacity - start);
        return std::span<const uint8_t>(buf_.data() + start, n);
    }

    // Advance the consumer index by `n` bytes that have been drained.
    void consume(size_t n) noexcept {
        tail_.store(tail_.load(std::memory_order_relaxed) + n,
                    std::memory_order_release);
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_relaxed);
    }

    size_t occupancy() const noexcept {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_relaxed);
    }

    size_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    // head_: next write index (producer-owned), tail_: next read index
    // (consumer-owned). `alignas(64)` separates them physically.
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) std::atomic<size_t> dropped_{0};
    alignas(64) std::array<uint8_t, Capacity> buf_{};
};

}  // namespace bridge
