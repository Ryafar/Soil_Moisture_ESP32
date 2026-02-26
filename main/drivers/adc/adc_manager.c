/**
 * @file adc_shared.c
 * @brief Shared ADC Manager - Implementation
 * 
 * This module provides a shared ADC management system that allows multiple
 * modules to use the same ADC unit and channels efficiently. It uses reference
 * counting to manage the ADC unit lifecycle and provides channel-specific
 * configuration management.
 * 
 * ADC_CALI_SCHEME_VER_CURVE_FITTING and ADC_CALI_SCHEME_VER_LINE_FITTING are
 * defined by the ESP-IDF based on the target chip's capabilities. They do not
 * have to be defined manually.
 * 
 * ADC_CALI_SCHEME_VER_CURVE_FITTING: Used by the newer chips: ESP32-C3, C6, S3, H2, ...
 * ADC_CALI_SCHEME_VER_LINE_FITTING: Used by older chips: e.g. ESP32 Lolin Lite, ...
 */

#include "adc_manager.h"

#include "esp_log.h"
#include "soc/soc_caps.h"
#include <string.h>
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc_cal.h"  // Old calibration API for fallback

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
    
    // Delete any remaining calibration handles before deleting the unit
    for (int i = 0; i < ADC_SHARED_MAX_CHANNELS; i++) {
        if (shared_unit->channels[i].is_configured && shared_unit->channels[i].cali_handle != NULL) {
#if ADC_CALI_SCHEME_VER_CURVE_FITTING
            adc_cali_delete_scheme_curve_fitting(shared_unit->channels[i].cali_handle);
#endif
            shared_unit->channels[i].cali_handle = NULL;
        }
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
    shared_unit->channels[channel].use_characteristics = false;

    // Create calibration scheme handle for this channel
    // Only use CURVE_FITTING if available (newer chips like ESP32-C3, S3, etc.)
    // Older chips like ESP32 (Lolin Lite) will use old characteristic API
#if ADC_CALI_SCHEME_VER_CURVE_FITTING
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id  = unit,
        .chan     = channel,
        .atten    = attenuation,
        .bitwidth = bitwidth,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &shared_unit->channels[channel].cali_handle);
#else
    ret = ESP_ERR_NOT_SUPPORTED;  // Explicitly not supported, will use old API
#endif
    
    if (ret != ESP_OK) {
        // Fallback: use old characteristic-based calibration (esp_adc_cal API)
        ESP_LOGD(TAG, "CURVE_FITTING not available, trying old ADC calibration API...");
        esp_adc_cal_characterize(unit, attenuation, bitwidth, (int) (reference_voltage * 1000), 
                                 &shared_unit->channels[channel].charac);
        shared_unit->channels[channel].use_characteristics = true;
        shared_unit->channels[channel].cali_handle = NULL;
        ESP_LOGI(TAG, "ADC unit %d channel %d: using old characteristic-based calibration (V_ref=%.2fV)",
                 unit, channel, reference_voltage);
    } else {
        shared_unit->channels[channel].cali_handle = NULL;
        ESP_LOGI(TAG, "ADC unit %d channel %d: CURVE_FITTING calibration enabled",
                 unit, channel);
    }
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

    // Convert raw ADC value to voltage
    int voltage_mv = 0;
    
    if (ch_config->cali_handle != NULL) {
        // New API: CURVE_FITTING calibration
        ret = adc_cali_raw_to_voltage(ch_config->cali_handle, raw_value, &voltage_mv);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC unit %d channel %d: calibration conversion failed: %s",
                     unit, channel, esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGD(TAG, "ADC unit %d channel %d: CURVE_FITTING (raw=%d → %d mV)",
                 unit, channel, raw_value, voltage_mv);
    } else if (ch_config->use_characteristics) {
        // Old API: characteristic-based calibration
        voltage_mv = esp_adc_cal_raw_to_voltage(raw_value, &ch_config->charac);
        ESP_LOGD(TAG, "ADC unit %d channel %d: old characteristic API (raw=%d → %d mV)",
                 unit, channel, raw_value, voltage_mv);
    } else {
        // Fallback: linear approximation using VREF
        voltage_mv = (int)((raw_value * ch_config->reference_voltage * 1000.0f) / 4095.0f);
        ESP_LOGD(TAG, "ADC unit %d channel %d: linear fallback (raw=%d → %d mV, VREF=%.2f)",
                 unit, channel, raw_value, voltage_mv, ch_config->reference_voltage);
    }
    
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
    
    // Delete calibration handle if it exists
    if (shared_unit->channels[channel].cali_handle != NULL) {
#if ADC_CALI_SCHEME_VER_CURVE_FITTING
        adc_cali_delete_scheme_curve_fitting(shared_unit->channels[channel].cali_handle);
#elif ADC_CALI_SCHEME_VER_LINE_FITTING
        adc_cali_delete_scheme_line_fitting(shared_unit->channels[channel].cali_handle);
#endif
        shared_unit->channels[channel].cali_handle = NULL;
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