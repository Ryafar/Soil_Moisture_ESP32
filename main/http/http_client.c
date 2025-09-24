/**
 * @file http_client.c
 * @brief HTTP Client Implementation
 */

#include "http_client.h"
#include "../config/esp32-config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_system.h"
#include "sys/time.h"
#include <string.h>

static const char *TAG = "HTTPClient";

// Static variables
static http_client_config_t s_config;
static int s_last_status_code = 0;
static bool s_initialized = false;

// Forward declarations
static esp_err_t http_event_handler(esp_http_client_event_t *evt);
static uint64_t get_timestamp_ms(void);
static char* create_json_payload(const soil_data_packet_t* packet);

esp_err_t http_client_init(const http_client_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(http_client_config_t));
    s_initialized = true;
    
    ESP_LOGI(TAG, "HTTP client initialized for server %s:%d%s", 
             s_config.server_ip, s_config.server_port, s_config.endpoint);
    
    return ESP_OK;
}

esp_err_t http_client_deinit(void)
{
    s_initialized = false;
    ESP_LOGI(TAG, "HTTP client deinitialized");
    return ESP_OK;
}

http_response_status_t http_client_send_soil_data(const csm_v2_reading_t* reading, const char* device_id)
{
    if (!s_initialized || reading == NULL || device_id == NULL) {
        return HTTP_RESPONSE_ERROR;
    }

    // Create data packet
    soil_data_packet_t packet = {
        .timestamp = get_timestamp_ms(),
        .voltage = reading->voltage,
        .moisture_percent = reading->moisture_percent,
        .raw_adc = reading->raw_adc
    };
    
    strncpy(packet.device_id, device_id, sizeof(packet.device_id) - 1);
    packet.device_id[sizeof(packet.device_id) - 1] = '\0';

    return http_client_send_data_packet(&packet);
}

http_response_status_t http_client_send_data_packet(const soil_data_packet_t* packet)
{
    if (!s_initialized || packet == NULL) {
        return HTTP_RESPONSE_ERROR;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", 
             s_config.server_ip, s_config.server_port, s_config.endpoint);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = s_config.timeout_ms,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return HTTP_RESPONSE_ERROR;
    }

    // Create JSON payload
    char* json_payload = create_json_payload(packet);
    if (json_payload == NULL) {
        esp_http_client_cleanup(client);
        return HTTP_RESPONSE_ERROR;
    }

    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_payload, strlen(json_payload));

    ESP_LOGI(TAG, "Sending HTTP POST to %s", url);
    ESP_LOGD(TAG, "Payload: %s", json_payload);

    http_response_status_t result = HTTP_RESPONSE_ERROR;
    int retry_count = 0;

    while (retry_count <= s_config.max_retries) {
        esp_err_t err = esp_http_client_perform(client);
        
        if (err == ESP_OK) {
            s_last_status_code = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "HTTP POST Status = %d", s_last_status_code);
            
            if (s_last_status_code >= 200 && s_last_status_code < 300) {
                result = HTTP_RESPONSE_OK;
                break;
            } else {
                result = HTTP_RESPONSE_ERROR;
            }
        } else if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "HTTP request timeout");
            result = HTTP_RESPONSE_TIMEOUT;
        } else {
            ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
            result = HTTP_RESPONSE_NO_CONNECTION;
        }

        retry_count++;
        if (retry_count <= s_config.max_retries) {
            ESP_LOGW(TAG, "Retrying HTTP request (%d/%d)", retry_count, s_config.max_retries);
            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second before retry
        }
    }

    free(json_payload);
    esp_http_client_cleanup(client);
    
    return result;
}

http_response_status_t http_client_test_connection(void)
{
    if (!s_initialized) {
        return HTTP_RESPONSE_ERROR;
    }

    // Create a test data packet
    soil_data_packet_t test_packet = {
        .timestamp = get_timestamp_ms(),
        .voltage = 2.5f,
        .moisture_percent = 50.0f,
        .raw_adc = 2048
    };
    strcpy(test_packet.device_id, "TEST_CONNECTION");

    ESP_LOGI(TAG, "Testing HTTP connection...");
    return http_client_send_data_packet(&test_packet);
}

int http_client_get_last_status_code(void)
{
    return s_last_status_code;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

static uint64_t get_timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}

static char* create_json_payload(const soil_data_packet_t* packet)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }

    cJSON *timestamp = cJSON_CreateNumber((double)packet->timestamp);
    cJSON *voltage = cJSON_CreateNumber(packet->voltage);
    cJSON *moisture = cJSON_CreateNumber(packet->moisture_percent);
    cJSON *raw_adc = cJSON_CreateNumber(packet->raw_adc);
    cJSON *device_id = cJSON_CreateString(packet->device_id);

    cJSON_AddItemToObject(json, "timestamp", timestamp);
    cJSON_AddItemToObject(json, "voltage", voltage);
    cJSON_AddItemToObject(json, "moisture_percent", moisture);
    cJSON_AddItemToObject(json, "raw_adc", raw_adc);
    cJSON_AddItemToObject(json, "device_id", device_id);

    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);

    return json_string;
}