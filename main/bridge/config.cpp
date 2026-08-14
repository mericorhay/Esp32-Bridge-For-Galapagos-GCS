#include "bridge/config.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs_flash.h"

static const char* TAG = "cfg";

namespace bridge {

bool load_config(BridgeConfig& out) {
    out = kDefaultConfig;

    nvs_handle_t h;
    if (nvs_open("galapagos", NVS_READWRITE, &h) != ESP_OK) {
        // NVS partition missing is an erase-everything-to-recover state on
        // the target — the bridge still runs on defaults, so degrade
        // loudly instead of dying.
        ESP_LOGE(TAG, "NVS open failed; using defaults");
        return false;
    }

    size_t len = sizeof(out.ssid);
    if (nvs_get_str(h, "ssid", out.ssid, &len) == ESP_OK && len > 1) {
        ESP_LOGI(TAG, "ssid from NVS: %s", out.ssid);
    }

    len = sizeof(out.password);
    if (nvs_get_str(h, "pass", out.password, &len) == ESP_OK && len > 1) {
        ESP_LOGI(TAG, "password from NVS");
    }

    uint8_t ch = out.channel;
    if (nvs_get_u8(h, "channel", &ch) == ESP_OK) out.channel = ch;

    uint32_t baud = out.baud;
    if (nvs_get_u32(h, "baud", &baud) == ESP_OK) out.baud = baud;

    nvs_close(h);
    return true;
}

bool save_config(const BridgeConfig& cfg) {
    nvs_handle_t h;
    if (nvs_open("galapagos", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed; config not saved");
        return false;
    }
    bool ok = true;
    ok &= nvs_set_str(h, "ssid", cfg.ssid) == ESP_OK;
    ok &= nvs_set_str(h, "pass", cfg.password) == ESP_OK;
    ok &= nvs_set_u8(h, "channel", cfg.channel) == ESP_OK;
    ok &= nvs_set_u32(h, "baud", cfg.baud) == ESP_OK;
    ok &= nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (!ok) ESP_LOGE(TAG, "NVS write failed; config not saved");
    return ok;
}

}  // namespace bridge
