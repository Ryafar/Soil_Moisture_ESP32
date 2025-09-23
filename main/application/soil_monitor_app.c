/**
 * @file soil_monitor_app.c
 * @brief Soil Moisture Monitoring Application - Implementation
 */

#include "soil_monitor_app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "SOIL_MONITOR_APP";

// Task handle for the monitoring task
static TaskHandle_t monitoring_task_handle = NULL;

/**
 * @brief Soil monitoring task
 */
static void soil_monitoring_task(void* pvParameters) {
    soil_monitor_app_t* app = (soil_monitor_app_t*)pvParameters;
    csm_v2_reading_t reading;
    
    ESP_LOGI(TAG, "Soil monitoring task started");
    
    while (app->is_running) {
        esp_err_t ret = csm_v2_read(&app->sensor_driver, &reading);
        if (ret == ESP_OK) {
            if (app->config.enable_logging) {
                ESP_LOGI(TAG, "Soil Moisture: %.1f%% | Voltage: %.3fV | Raw ADC: %d",
                         reading.moisture_percent, reading.voltage, reading.raw_adc);
            }
        } else {
            ESP_LOGE(TAG, "Failed to read sensor: %s", esp_err_to_name(ret));
        }
        
        vTaskDelay(pdMS_TO_TICKS(app->config.measurement_interval_ms));
    }
    
    ESP_LOGI(TAG, "Soil monitoring task stopped");
    monitoring_task_handle = NULL;
    vTaskDelete(NULL);
}

void soil_monitor_get_default_config(soil_monitor_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    config->adc_unit = ADC_UNIT_1;
    config->adc_channel = ADC_CHANNEL_0;
    config->measurement_interval_ms = 1000;
    config->enable_logging = true;
    config->dry_calibration_voltage = 3.0f;
    config->wet_calibration_voltage = 1.0f;
}

esp_err_t soil_monitor_init(soil_monitor_app_t* app, const soil_monitor_config_t* config) {
    if (app == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&app->config, config, sizeof(soil_monitor_config_t));
    
    // Initialize sensor driver
    csm_v2_config_t sensor_config;
    csm_v2_get_default_config(&sensor_config, config->adc_unit, config->adc_channel);
    sensor_config.dry_voltage = config->dry_calibration_voltage;
    sensor_config.wet_voltage = config->wet_calibration_voltage;
    sensor_config.enable_calibration = true;
    
    esp_err_t ret = csm_v2_init(&app->sensor_driver, &sensor_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sensor driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    app->is_running = false;
    ESP_LOGI(TAG, "Soil monitoring application initialized");
    return ESP_OK;
}

esp_err_t soil_monitor_start(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (app->is_running) {
        ESP_LOGW(TAG, "Application already running");
        return ESP_OK;
    }
    
    app->is_running = true;
    
    // Create monitoring task
    BaseType_t task_created = xTaskCreate(
        soil_monitoring_task,
        "soil_monitor",
        4096,
        app,
        5,
        &monitoring_task_handle
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitoring task");
        app->is_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Soil monitoring application started");
    return ESP_OK;
}

esp_err_t soil_monitor_stop(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!app->is_running) {
        ESP_LOGW(TAG, "Application not running");
        return ESP_OK;
    }
    
    app->is_running = false;
    
    // Wait for task to finish
    while (monitoring_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "Soil monitoring application stopped");
    return ESP_OK;
}

esp_err_t soil_monitor_deinit(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Stop application if running
    esp_err_t ret = soil_monitor_stop(app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop application: %s", esp_err_to_name(ret));
    }
    
    // Deinitialize sensor driver
    ret = csm_v2_deinit(&app->sensor_driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize sensor driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Soil monitoring application deinitialized");
    return ESP_OK;
}

esp_err_t soil_monitor_calibrate(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Starting calibration sequence...");
    ESP_LOGI(TAG, "Place sensor in dry soil and wait for readings to stabilize");
    
    // Take dry reading
    vTaskDelay(pdMS_TO_TICKS(3000));  // Wait 3 seconds
    float dry_voltage;
    esp_err_t ret = csm_v2_read_voltage(&app->sensor_driver, &dry_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read dry voltage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Dry voltage recorded: %.3fV", dry_voltage);
    ESP_LOGI(TAG, "Now place sensor in wet soil and wait for readings to stabilize");
    
    // Take wet reading
    vTaskDelay(pdMS_TO_TICKS(5000));  // Wait 5 seconds
    float wet_voltage;
    ret = csm_v2_read_voltage(&app->sensor_driver, &wet_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read wet voltage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Wet voltage recorded: %.3fV", wet_voltage);
    
    // Apply calibration
    ret = csm_v2_calibrate(&app->sensor_driver, dry_voltage, wet_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply calibration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Update app config
    app->config.dry_calibration_voltage = dry_voltage;
    app->config.wet_calibration_voltage = wet_voltage;
    
    ESP_LOGI(TAG, "Calibration completed successfully!");
    return ESP_OK;
}