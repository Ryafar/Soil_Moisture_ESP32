/**
 * @file http_client.c
 * @brief HTTP Client Implementation
 */

#include "http_client.h"

static const char *TAG = "HTTPClient";

// NVS buffering constants
#define HTTP_BUFFER_NAMESPACE "http_buffer"
#define HTTP_BUFFER_COUNT_KEY "pkt_count"
#define HTTP_BUFFER_PACKET_KEY "pkt_%03d"
#define MAX_PACKET_SIZE 1024
#define DEFAULT_MAX_BUFFERED_PACKETS 50

// Static variables
static http_client_config_t s_config;
static int s_last_status_code = 0;
static bool s_initialized = false;
static esp_http_client_handle_t s_persistent_client = NULL;
static nvs_handle_t s_nvs_handle = 0;
static bool s_buffering_enabled = false;

// Forward declarations
static esp_err_t http_event_handler(esp_http_client_event_t *evt);

esp_err_t http_client_init(const http_client_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(http_client_config_t));
    
    // Create persistent HTTP client
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d", 
             s_config.server_ip, s_config.server_port);

    esp_http_client_config_t client_config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = s_config.timeout_ms,
        .method = HTTP_METHOD_POST,
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
    };

    s_persistent_client = esp_http_client_init(&client_config);
    if (s_persistent_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize persistent HTTP client");
        return ESP_FAIL;
    }

    s_initialized = true;
    s_buffering_enabled = s_config.enable_buffering;
    
    // Initialize NVS for buffering if enabled
    if (s_buffering_enabled) {
        esp_err_t nvs_ret = nvs_open(HTTP_BUFFER_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
        if (nvs_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to open NVS for buffering: %s", esp_err_to_name(nvs_ret));
            s_buffering_enabled = false;
        } else {
            ESP_LOGI(TAG, "HTTP buffering enabled (max %d packets)", s_config.max_buffered_packets);
        }
    }
    
    ESP_LOGI(TAG, "HTTP client initialized for server %s:%d%s with persistent connection", 
             s_config.server_ip, s_config.server_port, s_config.endpoint);
    
    return ESP_OK;
}

esp_err_t http_client_deinit(void)
{
    if (s_persistent_client) {
        esp_http_client_cleanup(s_persistent_client);
        s_persistent_client = NULL;
    }
    
    // Close NVS handle
    if (s_buffering_enabled && s_nvs_handle != 0) {
        nvs_close(s_nvs_handle);
        s_nvs_handle = 0;
    }
    
    s_initialized = false;
    s_buffering_enabled = false;
    ESP_LOGI(TAG, "HTTP client deinitialized");
    return ESP_OK;
}

http_response_status_t http_client_send_json(const char* json_payload)
{
    if (!s_initialized || json_payload == NULL || s_persistent_client == NULL) {
        return HTTP_RESPONSE_ERROR;
    }

    // Build full URL with endpoint
    char full_url[256];
    snprintf(full_url, sizeof(full_url), "http://%s:%d%s", 
             s_config.server_ip, s_config.server_port, s_config.endpoint);

    // Set the specific endpoint for this request
    esp_http_client_set_url(s_persistent_client, full_url);

    // Set headers and payload for this request
    esp_http_client_set_header(s_persistent_client, "Content-Type", "application/json");
    esp_http_client_set_post_field(s_persistent_client, json_payload, strlen(json_payload));

    ESP_LOGD(TAG, "Sending HTTP POST to %s", full_url);
    ESP_LOGD(TAG, "Payload: %s", json_payload);

    http_response_status_t result = HTTP_RESPONSE_ERROR;
    int retry_count = 0;

    while (retry_count <= s_config.max_retries) {
        esp_err_t err = esp_http_client_perform(s_persistent_client);
        
        if (err == ESP_OK) {
            s_last_status_code = esp_http_client_get_status_code(s_persistent_client);
            ESP_LOGD(TAG, "HTTP POST Status = %d", s_last_status_code);
            
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
    
    return result;
}

http_response_status_t http_client_test_connection(void)
{
    if (!s_initialized) {
        return HTTP_RESPONSE_ERROR;
    }

    // Create a simple test JSON payload
    // TODO: Adjust server to give better response for invalid data packet format
    const char* test_json = "{"
        "\"timestamp\": 1234567890000,"
        "\"voltage\": 3.1415,"
        "\"moisture_percent\": 3.1415,"
        "\"raw_adc\": 31415,"
        "\"device_id\": \"TEST_CONNECTION\""
        "}";

    ESP_LOGI(TAG, "Testing HTTP connection...");
    return http_client_send_json(test_json);
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

// Buffering functions implementation

static esp_err_t buffer_packet(const char* json_payload)
{
    if (!s_buffering_enabled || s_nvs_handle == 0 || json_payload == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get current packet count
    int32_t packet_count = 0;
    size_t required_size = sizeof(packet_count);
    esp_err_t ret = nvs_get_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, &packet_count);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        packet_count = 0;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get packet count: %s", esp_err_to_name(ret));
        return ret;
    }

    // Check buffer limit
    int32_t max_packets = (s_config.max_buffered_packets > 0) ? 
                      s_config.max_buffered_packets : DEFAULT_MAX_BUFFERED_PACKETS;
    
    if (packet_count >= max_packets) {
        ESP_LOGW(TAG, "Buffer full (%ld packets), dropping oldest", (long)packet_count);
        // Remove oldest packet (packet 0) and shift others down
        for (int32_t i = 0; i < packet_count - 1; i++) {
            char old_key[32], new_key[32];
            snprintf(old_key, sizeof(old_key), HTTP_BUFFER_PACKET_KEY, (int)(i + 1));
            snprintf(new_key, sizeof(new_key), HTTP_BUFFER_PACKET_KEY, (int)i);
            
            size_t blob_size = MAX_PACKET_SIZE;
            char temp_buffer[MAX_PACKET_SIZE];
            ret = nvs_get_blob(s_nvs_handle, old_key, temp_buffer, &blob_size);
            if (ret == ESP_OK) {
                nvs_set_blob(s_nvs_handle, new_key, temp_buffer, blob_size);
            }
        }
        packet_count--;
    }

    // Store new packet
    char packet_key[32];
    snprintf(packet_key, sizeof(packet_key), HTTP_BUFFER_PACKET_KEY, (int)packet_count);
    
    size_t payload_len = strlen(json_payload);
    if (payload_len >= MAX_PACKET_SIZE - sizeof(uint32_t) - sizeof(uint16_t)) {
        ESP_LOGE(TAG, "Packet too large to buffer (%d bytes)", payload_len);
        return ESP_ERR_INVALID_SIZE;
    }

    // Create buffered packet structure
    http_buffered_packet_t* packet = malloc(sizeof(http_buffered_packet_t) + payload_len + 1);
    if (packet == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for buffered packet");
        return ESP_ERR_NO_MEM;
    }

    packet->timestamp = (uint32_t)(esp_utils_get_timestamp_ms()); // seconds since boot
    packet->payload_size = payload_len;
    strcpy(packet->payload, json_payload);

    size_t packet_size = sizeof(http_buffered_packet_t) + payload_len + 1;
    ret = nvs_set_blob(s_nvs_handle, packet_key, packet, packet_size);
    free(packet);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store buffered packet: %s", esp_err_to_name(ret));
        return ret;
    }

    // Update packet count
    packet_count++;
    ret = nvs_set_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, packet_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update packet count: %s", esp_err_to_name(ret));
        return ret;
    }

    // Commit changes
    nvs_commit(s_nvs_handle);
    
    ESP_LOGI(TAG, "Packet buffered (%ld/%ld packets stored)", (long)packet_count, (long)max_packets);
    return ESP_OK;
}

http_response_status_t http_client_send_json_buffered(const char* json_payload)
{
    if (!s_initialized || json_payload == NULL) {
        return HTTP_RESPONSE_ERROR;
    }

    // Try to send packet normally first
    http_response_status_t result = http_client_send_json(json_payload);
    
    if (result == HTTP_RESPONSE_OK) {
        // Success - also try to flush any buffered packets
        if (s_buffering_enabled) {
            esp_err_t flush_ret = http_client_flush_buffered_packets();
            if (flush_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to flush buffered packets: %s", esp_err_to_name(flush_ret));
            }
        }
        return result;
    }

    // Failed to send - buffer the packet if buffering is enabled
    if (s_buffering_enabled && (result == HTTP_RESPONSE_NO_CONNECTION || 
                                result == HTTP_RESPONSE_TIMEOUT || 
                                result == HTTP_RESPONSE_ERROR)) {
        esp_err_t buffer_ret = buffer_packet(json_payload);
        if (buffer_ret == ESP_OK) {
            ESP_LOGW(TAG, "Server unavailable, packet buffered for later transmission");
            return HTTP_RESPONSE_OK; // Return OK since we buffered successfully
        } else {
            ESP_LOGE(TAG, "Failed to buffer packet: %s", esp_err_to_name(buffer_ret));
        }
    }

    return result;
}

esp_err_t http_client_flush_buffered_packets(void)
{
    if (!s_buffering_enabled || s_nvs_handle == 0) {
        return ESP_OK; // Nothing to flush
    }

    int32_t packet_count = http_client_get_buffered_packet_count();
    if (packet_count <= 0) {
        return ESP_OK; // No packets to flush
    }

    ESP_LOGI(TAG, "Flushing %ld buffered packets...", (long)packet_count);
    
    int32_t sent_count = 0;
    int32_t failed_count = 0;

    for (int32_t i = 0; i < packet_count; i++) {
        char packet_key[32];
        snprintf(packet_key, sizeof(packet_key), HTTP_BUFFER_PACKET_KEY, (int)i);
        
        // First check if the packet exists
        size_t required_size = 0;
        esp_err_t check_ret = nvs_get_blob(s_nvs_handle, packet_key, NULL, &required_size);
        if (check_ret != ESP_OK || required_size == 0) {
            ESP_LOGD(TAG, "Packet %ld does not exist, skipping", (long)i);
            continue; // Skip non-existent packets without counting as failure
        }
        
        size_t packet_size = MAX_PACKET_SIZE;
        http_buffered_packet_t* packet = malloc(MAX_PACKET_SIZE);
        if (packet == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for packet flush");
            failed_count++;
            continue;
        }

        esp_err_t ret = nvs_get_blob(s_nvs_handle, packet_key, packet, &packet_size);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read buffered packet %ld: %s", (long)i, esp_err_to_name(ret));
            free(packet);
            failed_count++;
            continue;
        }

        // Try to send the buffered packet
        http_response_status_t send_result = http_client_send_json(packet->payload);
        if (send_result == HTTP_RESPONSE_OK) {
            sent_count++;
            // Remove successful packet from storage
            nvs_erase_key(s_nvs_handle, packet_key);
        } else {
            ESP_LOGW(TAG, "Failed to send buffered packet %ld, keeping in buffer", (long)i);
            failed_count++;
        }
        
        free(packet);
        
        // Small delay between packets to avoid overwhelming server
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Compact the buffer by removing gaps
    if (sent_count > 0) {
        int32_t new_count = 0;
        for (int32_t i = 0; i < packet_count; i++) {
            char packet_key[32];
            snprintf(packet_key, sizeof(packet_key), HTTP_BUFFER_PACKET_KEY, (int)i);
            
            size_t packet_size = MAX_PACKET_SIZE;
            char temp_buffer[MAX_PACKET_SIZE];
            esp_err_t ret = nvs_get_blob(s_nvs_handle, packet_key, temp_buffer, &packet_size);
            if (ret == ESP_OK) {
                if (new_count != i) {
                    char new_key[32];
                    snprintf(new_key, sizeof(new_key), HTTP_BUFFER_PACKET_KEY, (int)new_count);
                    nvs_set_blob(s_nvs_handle, new_key, temp_buffer, packet_size);
                    nvs_erase_key(s_nvs_handle, packet_key);
                }
                new_count++;
            }
        }
        
        // Update packet count
        nvs_set_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, new_count);
        nvs_commit(s_nvs_handle);
        
        ESP_LOGI(TAG, "Flush complete: %ld sent, %ld failed, %ld remaining", 
                 (long)sent_count, (long)failed_count, (long)new_count);
    }

    return (failed_count == 0) ? ESP_OK : ESP_FAIL;
}

int32_t http_client_get_buffered_packet_count(void)
{
    if (!s_buffering_enabled || s_nvs_handle == 0) {
        return 0;
    }

    int32_t stored_count = 0;
    esp_err_t ret = nvs_get_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, &stored_count);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get buffered packet count: %s", esp_err_to_name(ret));
        return 0;
    }
    
    // Validate that packets actually exist and count real packets
    int32_t actual_count = 0;
    for (int32_t i = 0; i < stored_count; i++) {
        char packet_key[32];
        snprintf(packet_key, sizeof(packet_key), HTTP_BUFFER_PACKET_KEY, (int)i);
        
        size_t required_size = 0;
        ret = nvs_get_blob(s_nvs_handle, packet_key, NULL, &required_size);
        if (ret == ESP_OK && required_size > 0) {
            actual_count++;
        }
    }
    
    // If count mismatch detected, correct it
    if (actual_count != stored_count) {
        ESP_LOGW(TAG, "Packet count mismatch detected: stored=%ld, actual=%ld. Correcting...", 
                 (long)stored_count, (long)actual_count);
        nvs_set_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, actual_count);
        nvs_commit(s_nvs_handle);
    }
    
    return actual_count;
}

esp_err_t http_client_clear_buffered_packets(void)
{
    if (!s_buffering_enabled || s_nvs_handle == 0) {
        return ESP_OK;
    }

    int32_t packet_count = http_client_get_buffered_packet_count();
    
    // Clear all packet entries
    for (int32_t i = 0; i < packet_count; i++) {
        char packet_key[32];
        snprintf(packet_key, sizeof(packet_key), HTTP_BUFFER_PACKET_KEY, (int)i);
        nvs_erase_key(s_nvs_handle, packet_key);
    }
    
    // Reset packet count
    esp_err_t ret = nvs_set_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    
    nvs_commit(s_nvs_handle);
    ESP_LOGI(TAG, "Cleared %ld buffered packets", (long)packet_count);
    
    return ESP_OK;
}