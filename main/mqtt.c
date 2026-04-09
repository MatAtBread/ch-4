#include "mqtt.h"
#include "config.h"
#include "relays.h"
#include "ota_http.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include <string.h>

extern const char *TAG;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static char device_topic_base[64] = {0};

static void publish_state(void) {
    if (!mqtt_client || !device_topic_base[0]) return;
    
    ch4_config_t cfg;
    if (!config_load(&cfg)) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mode", cfg.mode);
    cJSON_AddBoolToObject(root, "pause", cfg.pause);
    
    const char *writeable[] = {"mode", "pause"};
    cJSON *writeable_arr = cJSON_CreateStringArray(writeable, 2);
    cJSON_AddItemToObject(root, "writeable", writeable_arr);

    char *json_str = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(mqtt_client, device_topic_base, json_str, 0, 1, 1); // QoS 1, Retained
    
    free(json_str);
    cJSON_Delete(root);
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
                    // Clear the retained set topic
                    esp_mqtt_client_publish(mqtt_client, expected_topic, NULL, 0, 1, 0);
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

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}
