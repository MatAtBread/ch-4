#include "mqtt.h"
#include "config.h"
#include "relays.h"
#include "ota_http.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_app_desc.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

extern const char *TAG;
extern int blink_delay;

static esp_mqtt_client_handle_t mqtt_client = NULL;
static char device_topic_base[64] = {0};
static SemaphoreHandle_t publish_mutex = NULL;

static void publish_state(void) {
    if (!mqtt_client || !device_topic_base[0] || !publish_mutex) return;

    if (xSemaphoreTake(publish_mutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire lock for MQTT publish");
        return;
    }

    ch4_config_t cfg;
    if (!config_load(&cfg)) {
        xSemaphoreGive(publish_mutex);
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mode", cfg.mode);
    cJSON_AddBoolToObject(root, "pause", cfg.pause);

    cJSON *meta = cJSON_CreateObject();
    cJSON_AddStringToObject(meta, "name", cfg.device_name[0] ? cfg.device_name : "CH4");

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        char ip_addr[16];
        esp_ip4addr_ntoa(&ip_info.ip, ip_addr, sizeof(ip_addr));
        cJSON_AddStringToObject(meta, "ip", ip_addr);
    } else {
        cJSON_AddStringToObject(meta, "ip", "0.0.0.0");
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddNumberToObject(meta, "rssi", ap_info.rssi);
    } else {
        cJSON_AddNumberToObject(meta, "rssi", 0);
    }

    char versionDetail[110] = {0};
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        snprintf((char *)versionDetail, sizeof versionDetail, "%s %s %s",
               app_desc->version, app_desc->date, app_desc->time);
    }

    char *info_json = malloc(256);
    if (info_json) {
        snprintf(info_json, 256,
            "{\"model\":\"CH4\",\"state_version\":1,\"build\":\"%s\",\"writeable\":[\"mode\",\"pause\"]}",
            versionDetail);

        cJSON *info_node = cJSON_Parse(info_json);
        if (info_node) {
            cJSON_AddItemToObject(meta, "info", info_node);
        }
        free(info_json);
    }

    cJSON_AddItemToObject(root, "meta", meta);

    char *json_str = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(mqtt_client, device_topic_base, json_str, 0, 1, 1); // QoS 1, Retained

    free(json_str);
    cJSON_Delete(root);
    xSemaphoreGive(publish_mutex);
}

void process_state_json(const char *json_payload) {
    if (!json_payload) return;

    cJSON *root = cJSON_Parse(json_payload);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON state payload");
        return;
    }

    bool changed = false;
    ch4_config_t cfg;
    config_load(&cfg);

    cJSON *ota_item = cJSON_GetObjectItem(root, "ota");
    if (ota_item && cJSON_IsTrue(ota_item)) {
        ESP_LOGI(TAG, "OTA requested via JSON payload");
        blink_delay = 175; // faster blink to indicate OTA mode
        start_http_ota();
    }

    cJSON *mode_item = cJSON_GetObjectItem(root, "mode");
    if (mode_item && cJSON_IsString(mode_item)) {
        strncpy(cfg.mode, mode_item->valuestring, sizeof(cfg.mode) - 1);
        cfg.mode[sizeof(cfg.mode) - 1] = '\0';
        changed = true;
    }

    cJSON *pause_item = cJSON_GetObjectItem(root, "pause");
    if (pause_item && cJSON_IsBool(pause_item)) {
        cfg.pause = cJSON_IsTrue(pause_item);
        changed = true;
    }

    cJSON_Delete(root);

    if (changed) {
        config_save(&cfg);
        relays_apply(cfg.mode, cfg.pause);
        publish_state();
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected");
            if (device_topic_base[0]) {
                char topic_set[128];
                snprintf(topic_set, sizeof(topic_set), "%s/set", device_topic_base);
                esp_mqtt_client_subscribe(mqtt_client, topic_set, 1);
                publish_state();
            }
            break;

        case MQTT_EVENT_DATA:
            if (event->topic_len > 0 && event->data_len > 0) {
                char topic[128] = {0};
                char data[512] = {0}; // reasonable limit for control payloads
                strncpy(topic, event->topic, event->topic_len < sizeof(topic)-1 ? event->topic_len : sizeof(topic)-1);
                strncpy(data, event->data, event->data_len < sizeof(data)-1 ? event->data_len : sizeof(data)-1);

                char expected_topic[128];
                snprintf(expected_topic, sizeof(expected_topic), "%s/set", device_topic_base);

                if (strcmp(topic, expected_topic) == 0) {
                    process_state_json(data);
                    // Clear the retained set topic from the broker so it doesn't refire on next boot
                    esp_mqtt_client_publish(mqtt_client, expected_topic, "", 0, 1, 1);
                }
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected");
            break;

        default:
            break;
    }
}

static void mqtt_keepalive_task(void *pvParameter) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5 * 60 * 1000));
        publish_state();
    }
}

void mqtt_start(const char *url, const char *device_name) {
    snprintf(device_topic_base, sizeof(device_topic_base), "FreeHouse/%s", device_name);

    char full_uri[160];
    if (strncmp(url, "mqtt://", 7) == 0 || strncmp(url, "mqtts://", 8) == 0) {
        strncpy(full_uri, url, sizeof(full_uri) - 1);
    } else {
        snprintf(full_uri, sizeof(full_uri), "mqtt://%s", url);
    }
    full_uri[sizeof(full_uri)-1] = '\0';

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = full_uri;

    if (!publish_mutex) publish_mutex = xSemaphoreCreateMutex();

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    xTaskCreate(&mqtt_keepalive_task, "mqtt_keepalive", 4096, NULL, 5, NULL);
}
