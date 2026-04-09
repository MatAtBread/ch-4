#pragma once

#include "esp_err.h"
#include "esp_wifi.h"

/**
 * Initialise WiFi using the onboard PCB antenna.
 *
 * Drives the RF-switch GPIO lines so that the internal antenna is selected,
 * then calls esp_wifi_init() with the supplied configuration.  If NVS has not
 * been initialised yet the nvs_enable flag in the config is cleared
 * automatically to prevent downstream errors.
 *
 * @param config  WiFi init configuration (must not be NULL).
 * @return        ESP_OK on success, or a forwarded esp_wifi_init() error code.
 */
esp_err_t dev_wifi_init(wifi_init_config_t *config);

/**
 * Deinitialise WiFi and release the RF-switch GPIO lines.
 *
 * @return  ESP_OK on success, or a forwarded esp_wifi_deinit() error code.
 */
esp_err_t dev_wifi_deinit(void);
