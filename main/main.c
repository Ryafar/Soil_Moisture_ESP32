/**
 * @file main.c
 * @brief Battery Monitor application
 * 
 * This application monitors battery voltage and sends data to InfluxDB/MQTT.
 */

#include "main.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "application/battery_monitor.h"
#include "drivers/csm_v2_driver/csm_v2_driver.h"
#include "utils/esp_utils.h"
#include "esp_mac.h"
#include "config/esp32-config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

#define MEASUREMENT_TASK_STACK_SIZE 8192
#define MEASUREMENT_TASK_PRIORITY 5


// MARK: Measurement Task
/**
 * @brief Measurement task - handles battery monitoring, WiFi, data transmission
 */
static void measurement_task(void* pvParameters) {
    esp_err_t ret;
    
    ESP_LOGI(TAG, "=== Soil Moisture Sensor with Deep Sleep ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());

    
    // Check if this is a deep sleep wakeup
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "Wakeup caused by timer");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            ESP_LOGI(TAG, "First boot or reset (not a deep sleep wakeup)");
            break;
    }

    // Generate device ID from MAC address
    char device_id[32];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "ESP32C3_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);


    
    // ######################################################
    // MARK: Initialize
    // ######################################################
    
    // Soil sensor configuration
    csm_v2_config_t csm_config = {
        .adc_unit = SOIL_ADC_UNIT,
        .adc_channel = SOIL_ADC_CHANNEL,
        .esp_pin_power = SOIL_SENSOR_POWER_PIN,
        .dry_voltage = SOIL_DRY_VOLTAGE_DEFAULT,
        .wet_voltage = SOIL_WET_VOLTAGE_DEFAULT,
        .enable_calibration = false
    };
        ret = csm_v2_init(&csm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize CSM v2: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize battery monitoring
    ESP_LOGI(TAG, "Initializing battery monitoring...");
    ret = battery_monitor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize battery monitoring: %s", esp_err_to_name(ret));
        return;
    }





    // ######################################################
    // MARK: Main Measurement Loop
    // ######################################################
    
    ESP_LOGI(TAG, "Initialization complete. Starting measurements in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000)); // Short delay to ensure everything is initialized before starting measurements

    ESP_LOGI(TAG, "Starting main measurement loop...");

    { // Measuring
        ESP_LOGI(TAG, "=== Measurement Cycle ===");

        // ======== Measure Battery ========
        float battery_voltage = 0.0f;
        float battery_voltage_sum = 0.0f;
        int nr_measurements = 0;
        ret = ESP_OK;

        while (nr_measurements < BATTERY_ADC_MEASUREMENTS) {
            ret |= battery_monitor_measure(&battery_voltage);
            battery_voltage_sum += battery_voltage;
            nr_measurements++;
        }
        if (ret == ESP_OK) {
            battery_voltage = battery_voltage_sum / nr_measurements;
            ESP_LOGI(TAG, "Battery Voltage: %.3f V (average of %d measurements)", battery_voltage, nr_measurements);
        } else {
            ESP_LOGE(TAG, "Failed to measure battery voltage: %s", esp_err_to_name(ret));
        }


        // ======== Measure Soil ========
        csm_v2_reading_t soil_reading;
        csm_v2_reading_t soil_reading_sum = {0};
        nr_measurements = 0;
        ret = ESP_OK;
        ret |= csm_v2_enable_power();
        vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for sensor to stabilize

        while (nr_measurements < SOIL_ADC_MEASUREMENTS) {
            ret |= csm_v2_read(&soil_reading);
            soil_reading_sum.voltage += soil_reading.voltage;
            soil_reading_sum.moisture_percent += soil_reading.moisture_percent;
            soil_reading_sum.raw_adc += soil_reading.raw_adc;
            nr_measurements++;
        }
        ret |= csm_v2_disable_power();
        if (ret == ESP_OK) {
            soil_reading_sum.voltage /= nr_measurements;
            soil_reading_sum.moisture_percent /= nr_measurements;
            soil_reading_sum.raw_adc /= nr_measurements;
            ESP_LOGI(TAG, "Soil Voltage: %.3f V | Moisture: %.1f%% | Raw ADC: %d (average of %d measurements)", 
                     soil_reading_sum.voltage, soil_reading_sum.moisture_percent, soil_reading_sum.raw_adc, nr_measurements);
        } else {
            ESP_LOGE(TAG, "Failed to measure soil moisture: %s", esp_err_to_name(ret));
        }
    }

    // ######################################################
    // MARK: Wait for Data to be Sent
    // ######################################################
 


    
    // ######################################################
    // MARK: Cleanup and Deep Sleep
    // ######################################################
    
    
    // Check if deep sleep is enabled
    if (DEEP_SLEEP_ENABLED) {
        ESP_LOGI(TAG, "Preparing for deep sleep...");
        
        // Configure timer wakeup
        uint64_t sleep_time_us = (uint64_t)DEEP_SLEEP_DURATION_SECONDS * 1000000ULL;
        esp_sleep_enable_timer_wakeup(sleep_time_us);
        
        ESP_LOGI(TAG, "Entering deep sleep for %d seconds...", DEEP_SLEEP_DURATION_SECONDS);
        ESP_LOGI(TAG, "============================================");
        
        // Small delay before entering deep sleep
        vTaskDelay(pdMS_TO_TICKS(DEEP_SLEEP_WAKEUP_DELAY_MS));
        
        // Enter deep sleep
        esp_deep_sleep_start();
    } else {
        ESP_LOGI(TAG, "Deep sleep disabled, restarting in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    // Task complete, delete itself
    vTaskDelete(NULL);
}







void app_main(void) {
    ESP_LOGI(TAG, "=== Battery Monitor Application ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    // Create measurement task
    xTaskCreate(
        measurement_task,
        "measurement",
        MEASUREMENT_TASK_STACK_SIZE,
        NULL,
        MEASUREMENT_TASK_PRIORITY,
        NULL
    );
}
