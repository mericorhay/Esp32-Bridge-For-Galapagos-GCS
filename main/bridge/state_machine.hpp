#pragma once

// Link lifecycle as a compile-time transition table.
//
// The bridge has no UI and no user; the only thing it needs to know about
// its own state is whether to (a) relay bytes, (b) blink the LED a certain
// way, and (c) tell the app something is wrong. Encoding that as a table
// instead of scattered if/else gives two real wins:
//
//   1. A transition that was never declared is a *compile* error, not a
//      runtime fall-through into the wrong state.
//   2. The table is inspectable — you can read every path the firmware
//      can take in one screen, which is worth a lot when the "user" is a
//      pilot holding a flying aircraft.
//
// Duplicate (from, event) pairs are rejected by static_assert below, so a
// refactor that makes two handlers claim the same edge fails the build
// instead of silently picking one.

#include <array>
#include <cstddef>
#include <cstdint>

namespace bridge {

enum class State : uint8_t {
    Boot,
    WifiConnecting,
    WifiConnected,
    LinkUp,      // UART radio has produced traffic recently — relaying
    LinkSilent,  // radio silent past the watchdog threshold
    Error,
};

enum class Event : uint8_t {
    WifiUp,
    WifiDown,
    RadioData,      // bytes arrived on the UART (vehicle → app direction)
    RadioSilence,   // watchdog fired: no UART traffic in LinkTimeoutSeconds
    Fatal,
};

using Action = void (*)(State from, State to);

struct Transition {
    State from;
    Event on;
    State to;
    Action action;
};

constexpr size_t kLinkTimeoutSeconds = 5;

// Actions — defined in state_machine.cpp. Kept as free functions so the
// table stays constexpr-clean (no member-function pointers on a class
// that carries runtime state).
void on_enter(State from, State to);

constexpr std::array<Transition, 15> kTransitions = {{
    {State::Boot,          Event::WifiUp,       State::WifiConnected, on_enter},
    {State::WifiConnecting, Event::WifiUp,      State::WifiConnected, on_enter},
    {State::WifiConnected, Event::WifiDown,     State::WifiConnecting, on_enter},
    {State::WifiConnected, Event::RadioData,    State::LinkUp,        on_enter},
    {State::LinkUp,        Event::RadioData,    State::LinkUp,        on_enter},
    {State::LinkUp,        Event::RadioSilence, State::LinkSilent,    on_enter},
    {State::LinkSilent,    Event::RadioData,    State::LinkUp,        on_enter},
    {State::LinkSilent,    Event::Fatal,        State::Error,         on_enter},
    // Fatal is reachable from every pre-Error state — `app_main` dispatches
    // it when AP bring-up or pipeline bring-up fails, at which point the
    // machine may still be in Boot/WifiConnecting/WifiConnected/LinkUp.
    {State::Boot,          Event::Fatal,        State::Error,         on_enter},
    {State::WifiConnecting, Event::Fatal,       State::Error,         on_enter},
    {State::WifiConnected, Event::Fatal,        State::Error,         on_enter},
    {State::LinkUp,        Event::Fatal,        State::Error,         on_enter},
    {State::Error,         Event::WifiUp,       State::WifiConnected, on_enter},
    {State::Error,         Event::WifiDown,     State::WifiConnecting, on_enter},
    {State::Error,         Event::Fatal,        State::Error,         on_enter},
}};

// Compile-time validation: every (from, on) pair must be unique.
namespace detail {
constexpr bool has_duplicate_transitions() {
    for (size_t i = 0; i < kTransitions.size(); ++i) {
        for (size_t j = i + 1; j < kTransitions.size(); ++j) {
            if (kTransitions[i].from == kTransitions[j].from &&
                kTransitions[i].on == kTransitions[j].on) {
                return true;
            }
        }
    }
    return false;
}
}  // namespace detail

static_assert(!detail::has_duplicate_transitions(),
              "state machine table contains duplicate (from, event) edges");

class StateMachine {
  public:
    State current() const noexcept { return state_; }

    // Dispatch an event. No-op (returns false) for edges the table doesn't
    // declare — the machine holds its state rather than inventing one.
    bool dispatch(Event ev) noexcept {
        for (const auto& t : kTransitions) {
            if (t.from == state_ && t.on == ev) {
                const State from = state_;
                state_ = t.to;
                if (t.action) t.action(from, t.to);
                return true;
            }
        }
        return false;
    }

  private:
    State state_ = State::Boot;
};

}  // namespace bridge
