#pragma once

#include <stdbool.h>

typedef struct {
    char ssid[64];
    char wifi_pwd[64];
    char mqtt_url[128];
    char device_name[32];
    char mode[8]; // "on", "off", "clock"
    bool pause;
} ch4_config_t;

#ifdef __cplusplus
extern "C" {
#endif

// Loads config from NVS. Returns true if fully populated, false if brand new/missing required fields.
bool config_load(ch4_config_t *cfg);

// Saves config fields to NVS.
void config_save(const ch4_config_t *cfg);

#ifdef __cplusplus
}
#endif
