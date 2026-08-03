#include "bridge/state_machine.hpp"

#include "esp_log.h"

static const char* TAG = "sm";

namespace bridge {

const char* state_name(State s) {
    switch (s) {
        case State::Boot:            return "boot";
        case State::WifiConnecting:  return "wifi-connecting";
        case State::WifiConnected:   return "wifi-up";
        case State::LinkUp:          return "link-up";
        case State::LinkSilent:      return "link-silent";
        case State::Error:           return "error";
    }
    return "?";
}

void on_enter(State from, State to) {
    ESP_LOGI(TAG, "%s -> %s", state_name(from), state_name(to));
}

}  // namespace bridge
