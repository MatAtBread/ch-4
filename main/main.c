/*
 * main.c – CH-4 four-channel mains relay controller
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_timer.h"

#include "board.h"
#include "config.h"
#include "relays.h"
#include "mqtt.h"
#include "web.h"

#ifdef CONFIG_BLINK_LED_STRIP
#include "led_strip.h"
#endif

const char *TAG = "CH-4";

int blink_delay = 5000;

/* ── LED ────────────────────────────────────────────────────────────────── */
#define BLINK_GPIO CONFIG_BLINK_GPIO

static void blink_led(int state) {
    gpio_set_level(BLINK_GPIO, !state);
}
static void configure_led(void) {
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

/* ── NVS ─────────────────────────────────────────────────────────────────── */
static void nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated – erasing and reinitialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* ── WiFi STA ───────────────────────────────────────────────────────────── */
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Time-based retry: keep trying for 5 minutes before falling back to captive portal.
// This also ensures post-boot reconnection keeps working after a transient outage —
// the timer resets on each successful IP acquisition.
static int64_t wifi_fail_start_us = 0;
#define WIFI_RETRY_TIMEOUT_US (5LL * 60 * 1000 * 1000)

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        int64_t now = esp_timer_get_time();
        if (wifi_fail_start_us == 0) wifi_fail_start_us = now;
        if (now - wifi_fail_start_us < WIFI_RETRY_TIMEOUT_US) {
            esp_wifi_connect();
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            wifi_fail_start_us = 0;
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGW(TAG, "WiFi failed after 5 min, falling back to captive portal");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_fail_start_us = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool connect_sta(ch4_config_t *cfg) {
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    char hostname[64];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Lowercase the device name for the hostname fallback
    char lower_dev[32] = {0};
    if (cfg->device_name[0]) {
        for (int i=0; cfg->device_name[i] && i < sizeof(lower_dev)-1; i++) {
            lower_dev[i] = tolower((unsigned char)cfg->device_name[i]);
        }
        snprintf(hostname, sizeof(hostname), "freehouse-%s", lower_dev);
    } else {
        snprintf(hostname, sizeof(hostname), "freehouse-ch4");
    }
    esp_netif_set_hostname(netif, hostname);

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(dev_wifi_init(&wifi_init));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .threshold = { .authmode = WIFI_AUTH_WPA2_PSK }
        },
    };
    strncpy((char*)wifi_config.sta.ssid, cfg->ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, cfg->wifi_pwd, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

/* ── Entry point ─────────────────────────────────────────────────────────── */
void app_main(void) {
    ESP_LOGI(TAG, "CH-4 relay controller starting");

    nvs_init();
    configure_led();
    relays_init();

    ch4_config_t cfg;
    bool has_config = config_load(&cfg);

    // Always apply loaded (or default) relays state to GPIO immediately on boot
    relays_apply(cfg.mode, cfg.pause);

    if (!has_config) {
        ESP_LOGW(TAG, "Missing configuration, falling back to Captive Portal");
        blink_delay = 125;

        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        captive_portal_start();

    } else {
        ESP_LOGI(TAG, "Config loaded. Connecting to %s", cfg.ssid);
        if (connect_sta(&cfg)) {
            ESP_LOGI(TAG, "Starting MQTT and background web interface");
            // Start background config so we can still change it without wiping
            web_config_start();
            mqtt_start(cfg.mqtt_url, cfg.device_name[0] ? cfg.device_name : "CH4");
            blink_delay = 5000;
        } else {
            ESP_LOGW(TAG, "STA connection failed, falling back to Captive Portal");
            blink_delay = 125;
            // Teardown STA and boot soft AP
            esp_wifi_stop();
            esp_wifi_deinit();

            captive_portal_start();
        }
    }

    /* Main loop – heartbeat */
    while (1) {
        blink_led(1);
        vTaskDelay(pdMS_TO_TICKS(125));
        blink_led(0);
        vTaskDelay(pdMS_TO_TICKS(blink_delay));
    }
}
