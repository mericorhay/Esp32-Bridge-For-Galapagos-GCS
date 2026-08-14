#include <cstdint>

#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "bridge/app_state.hpp"
#include "bridge/config.hpp"
#include "bridge/state_machine.hpp"
#include "bridge/wifi_ap.hpp"

static const char* TAG = "main";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Galapagos bridge starting");

    // NVS must be up BEFORE load_config() — it reads the pilot's saved
    // SSID/password/baud overrides from flash. wifi_ap_start() used to do
    // this init, which put it AFTER load_config() and silently dropped every
    // override on every boot (nvs_open failed, defaults used).
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Partition was built for a different layout or got corrupted in
        // the field; wiping it is the only recovery, then retry once.
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s; overrides unavailable, using defaults",
                 esp_err_to_name(nvs_err));
    }

    bridge::BridgeConfig cfg;
    bridge::load_config(cfg);

    // LED pin is an output from the very start — the led_task shows the
    // boot state before WiFi is even up.
    gpio_config_t led_gpio = {};
    led_gpio.pin_bit_mask = 1ULL << bridge::kLedPin;
    led_gpio.mode = GPIO_MODE_OUTPUT;
    led_gpio.pull_up_en = GPIO_PULLUP_DISABLE;
    led_gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
    led_gpio.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&led_gpio);

    if (!bridge::wifi_ap_start(cfg)) {
        bridge::g_sm.dispatch(bridge::Event::Fatal);
        ESP_LOGE(TAG, "AP bring-up failed; bridge unusable");
        return;  // led_task never starts; the LED stays off = error
    }
    bridge::g_sm.dispatch(bridge::Event::WifiUp);

    if (!bridge::start_pipeline(cfg.baud)) {
        bridge::g_sm.dispatch(bridge::Event::Fatal);
        ESP_LOGE(TAG, "pipeline bring-up failed; bridge unusable");
        return;
    }

    // app_main returns; the FreeRTOS scheduler keeps running the tasks.
    // There is deliberately nothing left here to delete.
}
