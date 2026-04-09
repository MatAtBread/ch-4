/*
 * board.c – CH-4 board-level hardware abstraction
 *
 * Provides thin wrappers around esp_wifi_init / esp_wifi_deinit that drive the
 * onboard RF-switch GPIOs so that the PCB antenna is selected before the WiFi
 * stack starts.
 */

#include "board.h"

#include "driver/gpio.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_wifi.h"

extern const char *TAG;

/* RF-switch GPIO assignments for this board */
#define RF_SW_GPIO_A  3
#define RF_SW_GPIO_B  14

esp_err_t dev_wifi_init(wifi_init_config_t *config)
{
    /* Drive the RF-switch to select the onboard antenna */
    gpio_reset_pin(RF_SW_GPIO_A);
    gpio_set_direction(RF_SW_GPIO_A, GPIO_MODE_OUTPUT);
    gpio_set_level(RF_SW_GPIO_A, 0);

    gpio_reset_pin(RF_SW_GPIO_B);
    gpio_set_direction(RF_SW_GPIO_B, GPIO_MODE_OUTPUT);
    gpio_set_level(RF_SW_GPIO_B, 0);

    /*
     * If NVS has not been initialised yet, disable the NVS storage inside the
     * WiFi config to prevent esp_wifi_init() from returning an error.
     */
    nvs_handle_t check_handle;
    if (nvs_open("storage", NVS_READONLY, &check_handle) == ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGW(TAG, "NVS not initialised – disabling WiFi NVS storage");
        config->nvs_enable = 0;
    } else {
        nvs_close(check_handle);
    }

    esp_err_t err = esp_wifi_init(config);
    if (err == ESP_OK) {
        wifi_country_t country = {
            .cc     = "GB",
            .schan  = 1,
            .nchan  = 13,
            .policy = WIFI_COUNTRY_POLICY_MANUAL,
        };
        err = esp_wifi_set_country(&country);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dev_wifi_init failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t dev_wifi_deinit(void)
{
    /* Release the RF-switch pins when WiFi is torn down */
    gpio_reset_pin(RF_SW_GPIO_A);
    gpio_reset_pin(RF_SW_GPIO_B);
    return esp_wifi_deinit();
}
