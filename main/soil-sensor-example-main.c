/**
 * @file soil-sensor-example-main.c
 * @brief Main application entry point for Soil Moisture Sensor project
 * 
 * This file demonstrates the usage of the modular soil moisture sensor driver
 * and application layer.
 */

#include "soil-sensor-example-main.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "Starting Soil Moisture Sensor Application");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
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
    
    ESP_LOGI(TAG, "Initializing soil monitoring application...");
    esp_err_t ret = soil_monitor_init(&app, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize application: %s", esp_err_to_name(ret));
        return;
    }
    
    // Start monitoring
    ESP_LOGI(TAG, "Starting soil moisture monitoring...");
    ret = soil_monitor_start(&app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start monitoring: %s", esp_err_to_name(ret));
        soil_monitor_deinit(&app);
        return;
    }
    
    ESP_LOGI(TAG, "Application started successfully!");
    ESP_LOGI(TAG, "Monitoring soil moisture every %d ms", config.measurement_interval_ms);
    
    // Application will continue running in the background task
    // In a real application, you might want to add additional functionality here
    // such as WiFi connectivity, data logging, or user interfaces
}
