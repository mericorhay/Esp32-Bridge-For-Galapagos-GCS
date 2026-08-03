#include "bridge/wifi_ap.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"
#include "nvs_flash.h"

static const char* TAG = "wifi";

namespace bridge {

bool wifi_ap_start(const BridgeConfig& cfg) noexcept {
    // WiFi needs NVS for its own persistence (stored MAC, calibration).
    // The bridge otherwise never writes NVS except the pilot's overrides.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Partition was built for a different layout or got corrupted in
        // the field; wiping it is the only recovery, then retry once.
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
        return false;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    wifi_config_t ap = {};
    std::strncpy(reinterpret_cast<char*>(ap.ap.ssid), cfg.ssid,
                 sizeof(ap.ap.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(ap.ap.password), cfg.password,
                 sizeof(ap.ap.password) - 1);
    ap.ap.channel = cfg.channel;
    ap.ap.max_connection = 4;
    // Empty password means an open network — the config default is never
    // empty, but a pilot who insists on open WiFi gets it.
    ap.ap.authmode = std::strlen(cfg.password) > 0 ? WIFI_AUTH_WPA2_PSK
                                                   : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP '%s' up on channel %u (192.168.4.1)",
             cfg.ssid, cfg.channel);
    return true;
}

}  // namespace bridge
