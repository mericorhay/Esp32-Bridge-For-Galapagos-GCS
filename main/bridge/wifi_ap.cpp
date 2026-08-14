#include "bridge/wifi_ap.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"
#include "nvs_flash.h"

#include "bridge/demo.hpp"

static const char* TAG = "wifi";

namespace bridge {

// Learn the GCS's address the moment it joins the AP, so the demo can
// unicast telemetry to it (iOS is unreliable about broadcast). The event
// fires once per STA when DHCP hands out its IP.
static void ap_sta_ipassigned_handler(void*, esp_event_base_t, int32_t,
                                      void* event_data) {
    auto* data = static_cast<ip_event_ap_staipassigned_t*>(event_data);
    if (data == nullptr) return;
    uint32_t ip = data->ip.addr;
    ESP_LOGI(TAG, "STA assigned IP: %u.%u.%u.%u",
             ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    demo_set_peer(ip);
}

// When the GCS drops off the AP (phone sleeps, walks out of range, or the
// pilot closes the app), forget its IP so the demo does not keep unicasting
// telemetry at a stale address. The next STA to join gets its own
// IP_EVENT_AP_STAIPASSIGNED and re-arms the peer.
static void ap_sta_disconnected_handler(void*, esp_event_base_t, int32_t,
                                        void* event_data) {
    (void)event_data;
    ESP_LOGW(TAG, "STA disconnected; clearing peer IP");
    demo_set_peer(0);
}

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

    // Register before esp_wifi_start so we never miss a client. The
    // "STA assigned IP" event lives on IP_EVENT, not WIFI_EVENT.
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, ap_sta_ipassigned_handler,
        nullptr));
    // And the disconnect lives on WIFI_EVENT — clear the learned peer so
    // telemetry stops going to a stale address once the phone leaves.
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED,
        ap_sta_disconnected_handler, nullptr));

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
