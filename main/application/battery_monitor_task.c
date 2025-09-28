#include "battery_monitor_task.h"
#include "../config/esp32-config.h"
#include "../utils/esp_utils.h"
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

static adc_hal_t adc_handle;
static adc_hal_config_t adc_config;

esp_err_t battery_monitor_init() {
    
    // Initialize ADC
    adc_hal_get_default_config(&adc_config, BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL);
    esp_err_t ret = adc_hal_init(&adc_handle, &adc_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC for battery monitoring: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Battery monitor initialized");
    return ESP_OK;
}

esp_err_t battery_monitor_deinit() {
    // Deinitialize ADC
    esp_err_t ret = adc_hal_deinit(&adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize ADC for battery monitoring: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Battery monitor deinitialized");
    return ESP_OK;
}

esp_err_t battery_monitor_read_voltage(float* voltage) {
    if (voltage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = adc_hal_read_voltage(&adc_handle, voltage);
    *voltage = *voltage * BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR;
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read battery voltage: %s", esp_err_to_name(ret));
        return ret;
    }

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

    while (1) {
        float battery_voltage = 0;
        battery_monitor_read_voltage(&battery_voltage);

        // Log the reading
        printf("Battery Voltage: %.2f V\n", battery_voltage);

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