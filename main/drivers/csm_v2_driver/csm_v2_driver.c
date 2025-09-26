/**
 * @file csm-v2-driver.c
 * @brief Capacitive Soil Moisture Sensor V2 Driver - Implementation
 */

#include "csm-v2-driver.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "CSM_V2";

void csm_v2_get_default_config(csm_v2_config_t* config, adc_unit_t adc_unit, adc_channel_t adc_channel) {
    if (config == NULL) {
        return;
    }
    
    config->adc_unit = adc_unit;
    config->adc_channel = adc_channel;
    config->dry_voltage = 3.0f;         // Typical dry reading
    config->wet_voltage = 1.0f;         // Typical wet reading  
    config->enable_calibration = false;
}

esp_err_t csm_v2_init(csm_v2_driver_t* driver, const csm_v2_config_t* config) {
    if (driver == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&driver->config, config, sizeof(csm_v2_config_t));
    
    // Initialize ADC HAL
    adc_hal_config_t adc_config;
    adc_hal_get_default_config(&adc_config, config->adc_unit, config->adc_channel);
    
    esp_err_t ret = adc_hal_init(&driver->adc_hal, &adc_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC HAL: %s", esp_err_to_name(ret));
        return ret;
    }
    
    driver->is_initialized = true;
    ESP_LOGI(TAG, "CSM V2 driver initialized successfully");
    return ESP_OK;
}

esp_err_t csm_v2_deinit(csm_v2_driver_t* driver) {
    if (driver == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!driver->is_initialized) {
        ESP_LOGW(TAG, "Driver not initialized");
        return ESP_OK;
    }
    
    esp_err_t ret = adc_hal_deinit(&driver->adc_hal);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize ADC HAL: %s", esp_err_to_name(ret));
        return ret;
    }
    
    driver->is_initialized = false;
    ESP_LOGI(TAG, "CSM V2 driver deinitialized successfully");
    return ESP_OK;
}

esp_err_t csm_v2_read_voltage(csm_v2_driver_t* driver, float* voltage) {
    if (driver == NULL || voltage == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!driver->is_initialized) {
        ESP_LOGE(TAG, "Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = adc_hal_read_voltage(&driver->adc_hal, voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read voltage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "Voltage reading: %.3f V", *voltage);
    return ESP_OK;
}

esp_err_t csm_v2_read(csm_v2_driver_t* driver, csm_v2_reading_t* reading) {
    if (driver == NULL || reading == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!driver->is_initialized) {
        ESP_LOGE(TAG, "Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Read timestamp
    reading->timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds
    
    // Read raw ADC value
    esp_err_t ret = adc_hal_read_raw(&driver->adc_hal, &reading->raw_adc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read raw ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Read voltage
    ret = csm_v2_read_voltage(driver, &reading->voltage);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Calculate moisture percentage
    // Lower voltage = higher moisture (capacitive sensors)
    float voltage_range = driver->config.dry_voltage - driver->config.wet_voltage;
    if (voltage_range <= 0) {
        ESP_LOGW(TAG, "Invalid calibration values, using raw percentage");
        reading->moisture_percent = 0.0f;
    } else {
        float normalized = (driver->config.dry_voltage - reading->voltage) / voltage_range;
        reading->moisture_percent = normalized * 100.0f;
        
        // Clamp to 0-100%
        if (reading->moisture_percent < 0.0f) reading->moisture_percent = 0.0f;
        if (reading->moisture_percent > 100.0f) reading->moisture_percent = 100.0f;
    }
    
    ESP_LOGD(TAG, "Raw: %d, Voltage: %.3f V, Moisture: %.1f%%", 
             reading->raw_adc, reading->voltage, reading->moisture_percent);
    
    return ESP_OK;
}

esp_err_t csm_v2_calibrate(csm_v2_driver_t* driver, float dry_voltage, float wet_voltage) {
    if (driver == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (dry_voltage <= wet_voltage) {
        ESP_LOGE(TAG, "Invalid calibration values: dry_voltage must be > wet_voltage");
        return ESP_ERR_INVALID_ARG;
    }
    
    driver->config.dry_voltage = dry_voltage;
    driver->config.wet_voltage = wet_voltage;
    driver->config.enable_calibration = true;
    
    ESP_LOGI(TAG, "Calibration updated: Dry=%.3fV, Wet=%.3fV", dry_voltage, wet_voltage);
    return ESP_OK;
}