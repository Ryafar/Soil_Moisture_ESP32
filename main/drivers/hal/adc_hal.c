/**
 * @file adc_hal.c
 * @brief Hardware Abstraction Layer for ADC operations - Implementation
 */

#include "adc_hal.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#include <string.h>

static const char* TAG = "ADC_HAL";

// Static storage for shared ADC units (usually 2 for ESP32: ADC1 and ADC2)
#ifndef SOC_ADC_PERIPH_NUM
#define SOC_ADC_PERIPH_NUM 2
#endif

static adc_hal_shared_unit_t shared_units[SOC_ADC_PERIPH_NUM] = {0};

void adc_hal_get_default_config(adc_hal_config_t* config, adc_unit_t unit, adc_channel_t channel) {
    if (config == NULL) {
        return;
    }
    
    config->unit = unit;
    config->channel = channel;
    config->bitwidth = ADC_BITWIDTH_DEFAULT;
    config->attenuation = ADC_ATTEN_DB_11;  // 0-3.3V range
    config->reference_voltage = 3.3f;
}

esp_err_t adc_hal_init(adc_hal_t* adc_hal, const adc_hal_config_t* config) {
    if (adc_hal == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&adc_hal->config, config, sizeof(adc_hal_config_t));
    
    // Initialize ADC unit
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = config->unit,
    };
    
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_hal->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure ADC channel
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = config->bitwidth,
        .atten = config->attenuation,
    };
    
    ret = adc_oneshot_config_channel(adc_hal->handle, config->channel, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(adc_hal->handle);
        return ret;
    }
    
    ESP_LOGI(TAG, "ADC initialized successfully (Unit: %d, Channel: %d)", config->unit, config->channel);
    return ESP_OK;
}

esp_err_t adc_hal_deinit(adc_hal_t* adc_hal) {
    if (adc_hal == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = adc_oneshot_del_unit(adc_hal->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "ADC deinitialized successfully");
    return ESP_OK;
}

esp_err_t adc_hal_read_raw(adc_hal_t* adc_hal, int* raw_value) {
    if (adc_hal == NULL || raw_value == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = adc_oneshot_read(adc_hal->handle, adc_hal->config.channel, raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t adc_hal_read_voltage(adc_hal_t* adc_hal, float* voltage) {
    if (adc_hal == NULL || voltage == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    int raw_value;
    esp_err_t ret = adc_hal_read_raw(adc_hal, &raw_value);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Calculate maximum ADC value based on bitwidth
    int max_adc_value;
    switch (adc_hal->config.bitwidth) {
        case ADC_BITWIDTH_9:
            max_adc_value = 511;
            break;
        case ADC_BITWIDTH_10:
            max_adc_value = 1023;
            break;
        case ADC_BITWIDTH_11:
            max_adc_value = 2047;
            break;
        case ADC_BITWIDTH_12:
            max_adc_value = 4095;
            break;
        default:
            max_adc_value = 4095;  // Default to 12-bit
            break;
    }
    
    // Convert to voltage
    *voltage = ((float)raw_value / (float)max_adc_value) * adc_hal->config.reference_voltage;
    
    ESP_LOGD(TAG, "Raw: %d, Voltage: %.3f V", raw_value, *voltage);
    return ESP_OK;
}

// ============================================================================
// Shared ADC Manager Implementation
// ============================================================================

static adc_hal_shared_unit_t* get_shared_unit(adc_unit_t unit) {
    if (unit >= SOC_ADC_PERIPH_NUM) {
        return NULL;
    }
    return &shared_units[unit];
}

esp_err_t adc_hal_shared_init(adc_unit_t unit) {
    adc_hal_shared_unit_t* shared_unit = get_shared_unit(unit);
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
    for (int i = 0; i < ADC_HAL_MAX_CHANNELS; i++) {
        shared_unit->channels[i].is_configured = false;
    }
    
    ESP_LOGI(TAG, "Shared ADC unit %d initialized successfully", unit);
    return ESP_OK;
}

esp_err_t adc_hal_shared_deinit(adc_unit_t unit) {
    adc_hal_shared_unit_t* shared_unit = get_shared_unit(unit);
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

esp_err_t adc_hal_shared_add_channel(adc_unit_t unit, adc_channel_t channel, 
                                     adc_bitwidth_t bitwidth, adc_atten_t attenuation, 
                                     float reference_voltage) {
    adc_hal_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        ESP_LOGE(TAG, "Invalid ADC unit: %d", unit);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!shared_unit->is_initialized) {
        ESP_LOGE(TAG, "Shared ADC unit %d not initialized", unit);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= ADC_HAL_MAX_CHANNELS) {
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
    shared_unit->channels[channel].is_configured = true;
    
    ESP_LOGI(TAG, "ADC channel %d configured on unit %d successfully", channel, unit);
    return ESP_OK;
}

esp_err_t adc_hal_shared_read_raw(adc_unit_t unit, adc_channel_t channel, int* raw_value) {
    if (raw_value == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: raw_value is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    adc_hal_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        ESP_LOGE(TAG, "Invalid ADC unit: %d", unit);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!shared_unit->is_initialized) {
        ESP_LOGE(TAG, "Shared ADC unit %d not initialized", unit);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= ADC_HAL_MAX_CHANNELS || !shared_unit->channels[channel].is_configured) {
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

esp_err_t adc_hal_shared_read_voltage(adc_unit_t unit, adc_channel_t channel, float* voltage) {
    if (voltage == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: voltage is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    adc_hal_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        ESP_LOGE(TAG, "Invalid ADC unit: %d", unit);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (channel >= ADC_HAL_MAX_CHANNELS || !shared_unit->channels[channel].is_configured) {
        ESP_LOGE(TAG, "ADC channel %d not configured on unit %d", channel, unit);
        return ESP_ERR_INVALID_STATE;
    }
    
    int raw_value;
    esp_err_t ret = adc_hal_shared_read_raw(unit, channel, &raw_value);
    if (ret != ESP_OK) {
        return ret;
    }
    
    adc_hal_channel_config_t* ch_config = &shared_unit->channels[channel];
    
    // Calculate maximum ADC value based on bitwidth
    int max_adc_value;
    switch (ch_config->bitwidth) {
        case ADC_BITWIDTH_9:
            max_adc_value = 511;
            break;
        case ADC_BITWIDTH_10:
            max_adc_value = 1023;
            break;
        case ADC_BITWIDTH_11:
            max_adc_value = 2047;
            break;
        case ADC_BITWIDTH_12:
            max_adc_value = 4095;
            break;
        default:
            max_adc_value = 4095;  // Default to 12-bit
            break;
    }
    
    // Convert to voltage
    *voltage = ((float)raw_value / (float)max_adc_value) * ch_config->reference_voltage;
    
    ESP_LOGD(TAG, "ADC unit %d channel %d: Raw: %d, Voltage: %.3f V", unit, channel, raw_value, *voltage);
    return ESP_OK;
}

esp_err_t adc_hal_shared_remove_channel(adc_unit_t unit, adc_channel_t channel) {
    adc_hal_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        ESP_LOGE(TAG, "Invalid ADC unit: %d", unit);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (channel >= ADC_HAL_MAX_CHANNELS) {
        ESP_LOGE(TAG, "Invalid ADC channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    shared_unit->channels[channel].is_configured = false;
    ESP_LOGI(TAG, "ADC channel %d removed from unit %d", channel, unit);
    return ESP_OK;
}

bool adc_hal_shared_is_initialized(adc_unit_t unit) {
    adc_hal_shared_unit_t* shared_unit = get_shared_unit(unit);
    if (shared_unit == NULL) {
        return false;
    }
    return shared_unit->is_initialized;
}