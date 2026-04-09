#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

bool config_load(ch4_config_t *cfg) {
    memset(cfg, 0, sizeof(ch4_config_t));
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    size_t len;
    bool has_essential = true;

    len = sizeof(cfg->ssid);
    if (nvs_get_str(handle, "ssid", cfg->ssid, &len) != ESP_OK) has_essential = false;

    len = sizeof(cfg->wifi_pwd);
    if (nvs_get_str(handle, "wifipwd", cfg->wifi_pwd, &len) != ESP_OK) has_essential = false;

    len = sizeof(cfg->mqtt_url);
    if (nvs_get_str(handle, "mqtt", cfg->mqtt_url, &len) != ESP_OK) has_essential = false;

    len = sizeof(cfg->device_name);
    if (nvs_get_str(handle, "devname", cfg->device_name, &len) != ESP_OK) has_essential = false;

    len = sizeof(cfg->mode);
    if (nvs_get_str(handle, "mode", cfg->mode, &len) != ESP_OK) {
        strcpy(cfg->mode, "off");
    }

    uint8_t pb = 0;
    if (nvs_get_u8(handle, "pause", &pb) == ESP_OK) {
        cfg->pause = (pb != 0);
    } else {
        cfg->pause = false;
    }

    nvs_close(handle);
    return has_essential;
}

void config_save(const ch4_config_t *cfg) {
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        if (cfg->ssid[0]) nvs_set_str(handle, "ssid", cfg->ssid);
        if (cfg->wifi_pwd[0]) nvs_set_str(handle, "wifipwd", cfg->wifi_pwd);
        if (cfg->mqtt_url[0]) nvs_set_str(handle, "mqtt", cfg->mqtt_url);
        if (cfg->device_name[0]) nvs_set_str(handle, "devname", cfg->device_name);
        if (cfg->mode[0]) nvs_set_str(handle, "mode", cfg->mode);
        nvs_set_u8(handle, "pause", cfg->pause ? 1 : 0);
        
        nvs_commit(handle);
        nvs_close(handle);
    }
}
