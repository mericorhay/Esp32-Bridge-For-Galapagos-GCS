#include "bridge/app_state.hpp"

#include <cstdint>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bridge/state_machine.hpp"

static const char* TAG = "watch";

namespace bridge {

// The bridge never parses MAVLink (that's the app's job, and the whole
// point of the raw relay). It still needs to notice when the radio has
// gone quiet — ESP32's battery died, the SiK link dropped, someone kicked
// the power bank — so it watches the one signal it does understand: bytes
// flowing out of the ring toward the app. kLinkTimeoutSeconds of silence
// flips the state machine to LinkSilent, which the app mirrors with its
// own heartbeat watchdog once it stops receiving anything.
void link_watch_task(void*) {
    int64_t last_rx = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!g_rx_ring.empty()) {
            last_rx = esp_timer_get_time();
            continue;
        }
        int64_t silent_us = esp_timer_get_time() - last_rx;
        if (silent_us > static_cast<int64_t>(kLinkTimeoutSeconds) * 1'000'000) {
            if (g_sm.dispatch(Event::RadioSilence)) {
                ESP_LOGW(TAG, "radio silent for %d s", kLinkTimeoutSeconds);
            }
            // Avoid re-firing every 500 ms: reset the clock so the next
            // RadioSilence can only come after another full timeout.
            last_rx = esp_timer_get_time();
        }
    }
}

}  // namespace bridge
