/**
 * @file soil_project_main.c
 * @brief Main application entry point for Soil Moisture Sensor project
 * 
 * This file demonstrates the usage of the modular soil moisture sensor driver
 * and application layer with deep sleep power management.
 */

#include "soil_project_main.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "application/influx_sender.h"
#include "utils/ntp_time.h"

static const char *TAG = "MAIN";

// NTP sync callback
static void ntp_sync_callback(ntp_status_t status, const char* time_str) {
    switch (status) {
        case NTP_STATUS_SYNCED:
            ESP_LOGI(TAG, "NTP Time Synchronized! Swiss time: %s", time_str ? time_str : "N/A");
            break;
        case NTP_STATUS_FAILED:
            ESP_LOGW(TAG, "NTP Time Synchronization Failed");
            break;
        case NTP_STATUS_SYNCING:
            ESP_LOGI(TAG, "NTP Time Synchronizing...");
            break;
        default:
            break;
    }
}

void app_main(void) {
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
    
    // Initialize application
    static soil_monitor_app_t app;
    soil_monitor_config_t config;
    
    // Get default configuration
    soil_monitor_get_default_config(&config);
    
    // Customize configuration using project constants
    config.adc_unit = SOIL_ADC_UNIT;
    config.adc_channel = SOIL_ADC_CHANNEL;
    config.measurement_interval_ms = SOIL_MEASUREMENT_INTERVAL_MS;
    config.enable_logging = SOIL_ENABLE_DETAILED_LOGGING;
    config.dry_calibration_voltage = SOIL_DRY_VOLTAGE_DEFAULT;
    config.wet_calibration_voltage = SOIL_WET_VOLTAGE_DEFAULT;
    config.enable_wifi = true;
    config.enable_http_sending = true;
    config.measurements_per_cycle = SOIL_MEASUREMENTS_PER_CYCLE;  // Only X measurement(s) before deep sleep
    
    ESP_LOGI(TAG, "Initializing soil monitoring application...");
    esp_err_t ret = soil_monitor_init(&app, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize application: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize NTP time synchronization (after WiFi is connected) - if enabled
#if NTP_ENABLED
    ESP_LOGI(TAG, "Initializing NTP time synchronization...");
    ret = ntp_time_init(ntp_sync_callback);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize NTP: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "NTP initialization started, waiting for sync...");
        ret = ntp_time_wait_for_sync(NTP_SYNC_TIMEOUT_MS);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "NTP synchronized successfully!");
        } else {
            ESP_LOGW(TAG, "NTP sync timeout, will continue syncing in background");
        }
    }
#else
    ESP_LOGI(TAG, "NTP disabled - using server timestamps for InfluxDB");
#endif
    
    // Start soil monitoring task
    ESP_LOGI(TAG, "Starting soil moisture monitoring (%lu measurement(s))...", config.measurements_per_cycle);
    ret = soil_monitor_start(&app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start monitoring: %s", esp_err_to_name(ret));
        soil_monitor_deinit(&app);
        return;
    }
    
    // Start battery monitoring task
    ESP_LOGI(TAG, "Starting battery monitoring (%lu measurement(s))...", BATTERY_MEASUREMENTS_PER_CYCLE);
    ret = battery_monitor_start(BATTERY_MEASUREMENTS_PER_CYCLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start battery monitoring: %s", esp_err_to_name(ret));
        soil_monitor_deinit(&app);
        return;
    }
    
    // Wait for both tasks to complete their measurements
    ESP_LOGI(TAG, "Waiting for all measurements to complete...");
    
    // Wait for soil monitoring task (30 second timeout)
    ret = soil_monitor_wait_for_completion(&app, 30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Soil monitoring completion failed: %s", esp_err_to_name(ret));
    }
    
    // Wait for battery monitoring task (30 second timeout)
    ret = battery_monitor_wait_for_completion(30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery monitoring completion failed: %s", esp_err_to_name(ret));
    }
    
    // Wait for all InfluxDB data to be sent (30 second timeout)
    ESP_LOGI(TAG, "Waiting for all data to be sent to InfluxDB...");
    ret = influx_sender_wait_until_empty(30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "InfluxDB sender queue not empty: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "All measurements complete and data sent!");
    
    // Check if deep sleep is enabled
    if (DEEP_SLEEP_ENABLED) {
        ESP_LOGI(TAG, "Preparing for deep sleep...");
        
        // Clean up resources before deep sleep
        soil_monitor_deinit(&app);
        battery_monitor_deinit();
        
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
}
