/**
 * TODOs
 * - [ ] add own error types instead of esp errors
 */




/**
 * @file csm-v2-driver.c
 * @brief Capacitive Soil Moisture Sensor V2 Driver - Implementation
 */

#include "csm_v2_driver.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "CSM_V2";

esp_err_t csm_v2_get_default_config(csm_v2_config_t* config, adc_unit_t adc_unit, adc_channel_t adc_channel) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    config->adc_unit = adc_unit;
    config->adc_channel = adc_channel;
    config->dry_voltage = CSM_V2_DRY_VOLTAGE_DEFAULT;         // Typical dry reading
    config->wet_voltage = CSM_V2_WET_VOLTAGE_DEFAULT;         // Typical wet reading
    config->enable_calibration = false;
    return ESP_OK;
}

esp_err_t csm_v2_init(csm_v2_driver_t* driver, const csm_v2_config_t* config) {
    if (driver == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&driver->config, config, sizeof(csm_v2_config_t));
    
    // Initialize shared ADC unit
    esp_err_t ret = adc_hal_shared_init(config->adc_unit);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize shared ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Add soil sensor channel to shared ADC
    ret = adc_hal_shared_add_channel(config->adc_unit, config->adc_channel, 
                                     ADC_BITWIDTH_DEFAULT, ADC_ATTEN_DB_11, 3.3f);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add soil sensor channel to shared ADC: %s", esp_err_to_name(ret));
        adc_hal_shared_deinit(config->adc_unit);
        return ret;
    }
    
    driver->is_initialized = true;
    ESP_LOGI(TAG, "CSM V2 driver initialized successfully on ADC%d CH%d", 
             config->adc_unit + 1, config->adc_channel);
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
    
    // Remove soil sensor channel from shared ADC
    esp_err_t ret = adc_hal_shared_remove_channel(driver->config.adc_unit, driver->config.adc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove soil sensor channel from shared ADC: %s", esp_err_to_name(ret));
    }
    
    // Deinitialize shared ADC unit (will only actually deinit if ref count reaches 0)
    ret = adc_hal_shared_deinit(driver->config.adc_unit);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize shared ADC: %s", esp_err_to_name(ret));
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
    
    esp_err_t ret = adc_hal_shared_read_voltage(driver->config.adc_unit, driver->config.adc_channel, voltage);
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
    reading->timestamp = esp_utils_get_timestamp_ms();
    
    // Read raw ADC value
    esp_err_t ret = adc_hal_shared_read_raw(driver->config.adc_unit, driver->config.adc_channel, &reading->raw_adc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read raw ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Read voltage
    ret = csm_v2_read_voltage(driver, &reading->voltage);
    if (ret != ESP_OK) {
        return ret;
    }
    
    float percent = csm_v2_voltage_to_percent(driver, reading->voltage);
    reading->moisture_percent = percent;
    
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




// MARK: UTILS
float csm_v2_voltage_to_percent(csm_v2_driver_t* driver, float voltage) {
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float moisture_percent = 0;
    
    if (voltage >= driver->config.dry_voltage) {
        moisture_percent = 0.0f;
    } else if (voltage <= driver->config.wet_voltage) {
        moisture_percent = 100.0f;
    } else {
        moisture_percent = 
            ((driver->config.dry_voltage - voltage) / 
                (driver->config.dry_voltage - driver->config.wet_voltage)) * 100.0f;
    }
    
    // Clamp to [0, 100]
    if (moisture_percent < 0.0f) {
        moisture_percent = 0.0f;
    } else if (moisture_percent > 100.0f) {
        moisture_percent = 100.0f;
    }
    return moisture_percent;
}