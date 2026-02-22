/**
 * @file adc_shared.c
 * @brief Shared ADC Manager - Implementation
 * 
 * This module provides a shared ADC management system that allows multiple
 * modules to use the same ADC unit and channels efficiently. It uses reference
 * counting to manage the ADC unit lifecycle and provides channel-specific
 * configuration management.
 */

#include "adc_manager.h"

#include "esp_log.h"
#include "soc/soc_caps.h"
#include <string.h>
#include "esp_adc_cal.h"

static const char* TAG = "ADC_SHARED";

// Static storage for shared ADC units (usually 2 for ESP32: ADC1 and ADC2)
#ifndef SOC_ADC_PERIPH_NUM
#define SOC_ADC_PERIPH_NUM 2
#endif

static adc_shared_unit_t shared_units[SOC_ADC_PERIPH_NUM] = {0};

/**
 * @brief Get pointer to shared ADC unit structure
 * 
 * @param unit ADC unit
 * @return Pointer to shared unit structure, NULL if invalid unit
 */
static adc_shared_unit_t* get_shared_unit(adc_unit_t unit) {
    if (unit >= SOC_ADC_PERIPH_NUM) {
        return NULL;
    }
    return &shared_units[unit];
}

esp_err_t adc_shared_init(adc_unit_t unit) {
    adc_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        ESP_LOGE(TAG, "Invalid ADC unit: %d", unit);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (shared_unit->is_initialized) {
        shared_unit->ref_count++;
        ESP_LOGD(TAG, "Shared ADC unit %d ref count increased to %d", unit, shared_unit->ref_count);
        return ESP_OK;
    }
    
    // Initialize ADC unit
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = unit,
    };
    
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &shared_unit->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize shared ADC unit %d: %s", unit, esp_err_to_name(ret));
        return ret;
    }
    
    shared_unit->unit = unit;
    shared_unit->ref_count = 1;
    shared_unit->is_initialized = true;
    
    // Initialize all channels as not configured
    for (int i = 0; i < ADC_SHARED_MAX_CHANNELS; i++) {
        shared_unit->channels[i].is_configured = false;
    }
    
    ESP_LOGI(TAG, "Shared ADC unit %d initialized successfully", unit);
    return ESP_OK;
}

esp_err_t adc_shared_deinit(adc_unit_t unit) {
    adc_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        ESP_LOGE(TAG, "Invalid ADC unit: %d", unit);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!shared_unit->is_initialized) {
        ESP_LOGW(TAG, "Shared ADC unit %d not initialized", unit);
        return ESP_OK;
    }
    
    shared_unit->ref_count--;
    ESP_LOGD(TAG, "Shared ADC unit %d ref count decreased to %d", unit, shared_unit->ref_count);
    
    if (shared_unit->ref_count > 0) {
        return ESP_OK;  // Still in use by other modules
    }
    
    // Actually deinitialize when ref count reaches 0
    esp_err_t ret = adc_oneshot_del_unit(shared_unit->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize shared ADC unit %d: %s", unit, esp_err_to_name(ret));
        return ret;
    }
    
    shared_unit->is_initialized = false;
    ESP_LOGI(TAG, "Shared ADC unit %d deinitialized successfully", unit);
    return ESP_OK;
}

esp_err_t adc_shared_add_channel(adc_unit_t unit, adc_channel_t channel, 
                                 adc_bitwidth_t bitwidth, adc_atten_t attenuation, 
                                 float reference_voltage) {
    adc_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        ESP_LOGE(TAG, "Invalid ADC unit: %d", unit);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!shared_unit->is_initialized) {
        ESP_LOGE(TAG, "Shared ADC unit %d not initialized", unit);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= ADC_SHARED_MAX_CHANNELS) {
        ESP_LOGE(TAG, "Invalid ADC channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Configure ADC channel
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = bitwidth,
        .atten = attenuation,
    };
    
    esp_err_t ret = adc_oneshot_config_channel(shared_unit->handle, channel, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel %d on unit %d: %s", 
                 channel, unit, esp_err_to_name(ret));
        return ret;
    }
    
    // Store channel configuration
    shared_unit->channels[channel].channel = channel;
    shared_unit->channels[channel].bitwidth = bitwidth;
    shared_unit->channels[channel].attenuation = attenuation;
    shared_unit->channels[channel].reference_voltage = reference_voltage;
    // Characterize ADC for this channel (default_vref = 1100 mV, esp_adc_cal will read efuse if available)
    esp_adc_cal_characterize(unit, attenuation, bitwidth, 1100, &shared_unit->channels[channel].charac);
    shared_unit->channels[channel].is_configured = true;
    
    ESP_LOGI(TAG, "ADC channel %d configured on unit %d successfully", channel, unit);
    return ESP_OK;
}

esp_err_t adc_shared_read_raw(adc_unit_t unit, adc_channel_t channel, int* raw_value) {
    if (raw_value == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: raw_value is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    adc_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        ESP_LOGE(TAG, "Invalid ADC unit: %d", unit);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!shared_unit->is_initialized) {
        ESP_LOGE(TAG, "Shared ADC unit %d not initialized", unit);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= ADC_SHARED_MAX_CHANNELS || !shared_unit->channels[channel].is_configured) {
        ESP_LOGE(TAG, "ADC channel %d not configured on unit %d", channel, unit);
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = adc_oneshot_read(shared_unit->handle, channel, raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC channel %d on unit %d: %s", 
                 channel, unit, esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "ADC unit %d channel %d raw value: %d", unit, channel, *raw_value);
    return ESP_OK;
}

esp_err_t adc_shared_read_voltage(adc_unit_t unit, adc_channel_t channel, float* voltage) {
    if (voltage == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: voltage is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    adc_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        ESP_LOGE(TAG, "Invalid ADC unit: %d", unit);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (channel >= ADC_SHARED_MAX_CHANNELS || !shared_unit->channels[channel].is_configured) {
        ESP_LOGE(TAG, "ADC channel %d not configured on unit %d", channel, unit);
        return ESP_ERR_INVALID_STATE;
        
    }
    
    int raw_value;
    esp_err_t ret = adc_shared_read_raw(unit, channel, &raw_value);
    if (ret != ESP_OK) {
        return ret;
    }
    
    adc_shared_channel_config_t* ch_config = &shared_unit->channels[channel];
    
    // Convert raw ADC value to voltage using esp_adc_cal for proper calibration
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage((uint32_t)raw_value, &ch_config->charac);
    *voltage = ((float)voltage_mv) / 1000.0f; // voltage at ADC pin in volts
    
    ESP_LOGD(TAG, "ADC unit %d channel %d: Raw: %d, Voltage: %.3f V", unit, channel, raw_value, *voltage);
    return ESP_OK;
}

esp_err_t adc_shared_remove_channel(adc_unit_t unit, adc_channel_t channel) {
    adc_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        ESP_LOGE(TAG, "Invalid ADC unit: %d", unit);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (channel >= ADC_SHARED_MAX_CHANNELS) {
        ESP_LOGE(TAG, "Invalid ADC channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    shared_unit->channels[channel].is_configured = false;
    ESP_LOGI(TAG, "ADC channel %d removed from unit %d", channel, unit);
    return ESP_OK;
}

bool adc_shared_is_initialized(adc_unit_t unit) {
    adc_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        return false;
    }
    return shared_unit->is_initialized;
}