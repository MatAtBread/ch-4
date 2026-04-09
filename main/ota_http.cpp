#include "ota_http.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" const char *TAG;

static int64_t content_len = -1;
static int64_t content_read = 0;
static int lastPercent = -1;

typedef struct {
  esp_ota_handle_t handle;
  const esp_partition_t *partition;
} ota_data_t;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    ota_data_t *update = (ota_data_t *)evt->user_data;

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (content_len == -1) {
                    content_len = esp_http_client_get_content_length(evt->client);
                    ESP_LOGI(TAG, "content_len=%lld", content_len);
                }
                content_read += evt->data_len;
                int percent = (content_len > 0) ? ((content_read * 100) / content_len) : 0;
                if (percent != lastPercent) {
                    ESP_LOGI(TAG, "Download progress: %d%% (%lld of %lld)", percent, content_read, content_len);
                    lastPercent = percent;
                }
                esp_err_t err = esp_ota_write(update->handle, evt->data, evt->data_len);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_write failed: %d", err);
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            if (esp_ota_end(update->handle) == ESP_OK) {
                if (esp_ota_set_boot_partition(update->partition) == ESP_OK) {
                    ESP_LOGI(TAG, "Download complete, restarting");
                    esp_restart();
                } else {
                    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed");
                }
            } else {
                ESP_LOGE(TAG, "esp_ota_end failed");
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "Accept", "application/octet-stream");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}

static void ota_task(void *pvParameter) {
    const char *url_str = "http://files.mailed.me.uk/public/freehouse/CH4/esp32c6/ch-4.bin";
    ESP_LOGI(TAG, "Starting HTTP OTA from: %s", url_str);

    esp_http_client_config_t config = {};
    config.url = url_str;
    config.timeout_ms = 5000;
    config.event_handler = _http_event_handler;

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t update_handle = 0;
    if (esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed");
        vTaskDelete(NULL);
        return;
    }

    ota_data_t od = {.handle = update_handle, .partition = update_partition};
    config.user_data = &od;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64,
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    
    // Cleanup if failure
    vTaskDelete(NULL);
}

void start_http_ota(void) {
    xTaskCreate(&ota_task, "ota_task", 8192, NULL, 5, NULL);
}
