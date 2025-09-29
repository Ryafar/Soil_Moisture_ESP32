#include "battery_monitor_task.h"
#include "../config/esp32-config.h"
#include "../utils/esp_utils.h"
#include "../utils/ntp_time.h"
#include "../drivers/adc/adc_manager.h"
#include "../drivers/http/http_client.h"
#include "../drivers/wifi/wifi_manager.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "BATTERY_MONITOR_TASK";

// Task handle for the monitoring task
static TaskHandle_t monitoring_task_handle = NULL;

/**
 * @brief Create JSON payload for battery voltage data
 */
static char* create_battery_json_payload(float voltage, const char* device_id) {
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }

    // Use NTP timestamp if available, otherwise fallback to system timestamp
    uint64_t timestamp_ms;
    char iso_time_str[64];
    
    if (ntp_time_is_synced()) {
        timestamp_ms = ntp_time_get_timestamp_ms();
        esp_err_t ret = ntp_time_get_iso_string(iso_time_str, sizeof(iso_time_str));
        if (ret != ESP_OK) {
            strcpy(iso_time_str, "1970-01-01T00:00:00+00:00");
        }
    } else {
        timestamp_ms = esp_utils_get_timestamp_ms();
        strcpy(iso_time_str, "1970-01-01T00:00:00+00:00");  // Not synced
    }

    cJSON *timestamp = cJSON_CreateNumber((double)timestamp_ms);
    cJSON *iso_timestamp = cJSON_CreateString(iso_time_str);
    cJSON *battery_voltage = cJSON_CreateNumber(voltage);
    cJSON *device_id_json = cJSON_CreateString(device_id);
    cJSON *data_type = cJSON_CreateString("battery");

    cJSON_AddItemToObject(json, "timestamp", timestamp);
    cJSON_AddItemToObject(json, "iso_timestamp", iso_timestamp);
    cJSON_AddItemToObject(json, "voltage", battery_voltage);
    cJSON_AddItemToObject(json, "device_id", device_id_json);
    cJSON_AddItemToObject(json, "type", data_type);

    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);

    return json_string;
}

/**
 * @brief Send battery voltage reading to HTTP server
 */
static http_response_status_t battery_send_reading_to_server(float voltage, const char* device_id) {
    if (device_id == NULL) {
        return HTTP_RESPONSE_ERROR;
    }

    char* json_payload = create_battery_json_payload(voltage, device_id);
    if (json_payload == NULL) {
        ESP_LOGE(TAG, "Failed to create battery JSON payload");
        return HTTP_RESPONSE_ERROR;
    }

    http_response_status_t result = http_client_send_json(json_payload);
    
    free(json_payload);
    return result;
}

esp_err_t battery_monitor_init() {
    
    // Initialize shared ADC unit
    esp_err_t ret = adc_shared_init(BATTERY_ADC_UNIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize shared ADC unit for battery monitoring: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Add battery monitoring channel to shared ADC
    ret = adc_shared_add_channel(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, 
                                     BATTERY_ADC_BITWIDTH, BATTERY_ADC_ATTENUATION, 
                                     BATTERY_ADC_VREF);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add battery channel to shared ADC: %s", esp_err_to_name(ret));
        adc_shared_deinit(BATTERY_ADC_UNIT);
        return ret;
    }

    ESP_LOGI(TAG, "Battery monitor initialized on ADC%d CH%d", BATTERY_ADC_UNIT + 1, BATTERY_ADC_CHANNEL);
    return ESP_OK;
}

esp_err_t battery_monitor_deinit() {
    // Remove battery channel from shared ADC
    esp_err_t ret = adc_shared_remove_channel(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove battery channel from shared ADC: %s", esp_err_to_name(ret));
    }
    
    // Deinitialize shared ADC unit (will only actually deinit if ref count reaches 0)
    ret = adc_shared_deinit(BATTERY_ADC_UNIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize shared ADC for battery monitoring: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Battery monitor deinitialized");
    return ESP_OK;
}

esp_err_t battery_monitor_read_voltage(float* voltage) {
    if (voltage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = adc_shared_read_voltage(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read battery voltage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Apply voltage scale factor (for voltage divider)
    *voltage = *voltage * BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR;

    return ESP_OK;
}

esp_err_t battery_monitor_stop() {
    if (monitoring_task_handle == NULL) {
        ESP_LOGW(TAG, "Battery monitor task not running");
        return ESP_ERR_INVALID_STATE;
    }

    vTaskDelete(monitoring_task_handle);
    monitoring_task_handle = NULL;

    ESP_LOGI(TAG, "Battery monitor task stopped");
    return ESP_OK;
}

esp_err_t battery_monitor_start() {
    if (monitoring_task_handle != NULL) {
        ESP_LOGW(TAG, "Battery monitor task already running");
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t result = xTaskCreate(
        battery_monitor_task,
        "battery_monitor_task",
        BATTERY_MONITOR_TASK_STACK_SIZE, // Stack size
        NULL, // Parameters
        BATTERY_MONITOR_TASK_PRIORITY,    // Priority
        &monitoring_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create battery monitor task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Battery monitor task started");
    return ESP_OK;
}

void battery_monitor_task(void *pvParameters) {

    // turn on the led
    led_init(LED_GPIO_NUM);
    led_set_state(LED_GPIO_NUM, true);

    battery_monitor_init();

    // Generate device ID from MAC address for battery monitoring
    char device_id[32];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "BATT_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Battery monitor device ID: %s", device_id);

    while (1) {
        float battery_voltage = 0;
        battery_monitor_read_voltage(&battery_voltage);

        // Log the reading
        ESP_LOGI(TAG, "Battery Voltage: %.2f V", battery_voltage);

        // Send data via HTTP if WiFi is connected
        if (wifi_manager_is_connected()) {
            http_response_status_t http_status = battery_send_reading_to_server(battery_voltage, device_id);
            if (http_status == HTTP_RESPONSE_OK) {
                ESP_LOGD(TAG, "Battery data sent successfully to server");
            } else {
                ESP_LOGW(TAG, "Failed to send battery data to server (status: %d)", http_status);
            }
        } else {
            ESP_LOGW(TAG, "WiFi not connected, skipping HTTP transmission");
        }

        if (battery_voltage < BATTERY_MONITOR_LOW_VOLTAGE_THRESHOLD) {
            ESP_LOGW(TAG, "Battery voltage low: %.2f V", battery_voltage);
            ESP_LOGW(TAG, "Please recharge or replace the battery. Shutting down.");
            
            // Perform cleanup and shutdown
            if (BATTERY_MONITOR_USE_DEEP_SLEEP_ON_LOW_BATTERY) {
                ESP_LOGI(TAG, "Entering deep sleep mode to conserve power.");
                battery_monitor_deinit();
                esp_deep_sleep_start();
            }
        }

        // Wait for the next measurement interval
        vTaskDelay(pdMS_TO_TICKS(BATTERY_MONITOR_MEASUREMENT_INTERVAL_MS));
    }
}