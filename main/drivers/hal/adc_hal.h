/**
 * @file adc_hal.h
 * @brief Hardware Abstraction Layer for ADC operations
 * 
 * This module provides a clean interface for ADC initialization and configuration,
 * abstracting the ESP-IDF specific ADC operations.
 */

#ifndef ADC_HAL_H
#define ADC_HAL_H

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

// Maximum number of channels per ADC unit
#define ADC_HAL_MAX_CHANNELS 8

/**
 * @brief ADC configuration structure
 */
typedef struct {
    adc_unit_t unit;                    ///< ADC unit (ADC_UNIT_1 or ADC_UNIT_2)
    adc_channel_t channel;              ///< ADC channel
    adc_bitwidth_t bitwidth;            ///< ADC resolution
    adc_atten_t attenuation;            ///< ADC attenuation
    float reference_voltage;            ///< Reference voltage for calculations
} adc_hal_config_t;

/**
 * @brief ADC handle structure
 */
typedef struct {
    adc_oneshot_unit_handle_t handle;   ///< ESP-IDF ADC handle
    adc_hal_config_t config;            ///< ADC configuration
} adc_hal_t;

/**
 * @brief Initialize ADC with the given configuration
 * 
 * @param adc_hal Pointer to ADC handle structure
 * @param config ADC configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_init(adc_hal_t* adc_hal, const adc_hal_config_t* config);

/**
 * @brief Deinitialize ADC
 * 
 * @param adc_hal Pointer to ADC handle structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_deinit(adc_hal_t* adc_hal);

/**
 * @brief Read raw ADC value
 * 
 * @param adc_hal Pointer to ADC handle structure
 * @param raw_value Pointer to store raw ADC value
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_read_raw(adc_hal_t* adc_hal, int* raw_value);

/**
 * @brief Read ADC value and convert to voltage
 * 
 * @param adc_hal Pointer to ADC handle structure
 * @param voltage Pointer to store voltage value
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_read_voltage(adc_hal_t* adc_hal, float* voltage);

/**
 * @brief Get default ADC configuration
 * 
 * @param config Pointer to configuration structure to fill
 * @param unit ADC unit to use
 * @param channel ADC channel to use
 */
void adc_hal_get_default_config(adc_hal_config_t* config, adc_unit_t unit, adc_channel_t channel);

// ============================================================================
// Shared ADC Manager Functions
// ============================================================================

/**
 * @brief Channel configuration for shared ADC
 */
typedef struct {
    adc_channel_t channel;              ///< ADC channel
    adc_bitwidth_t bitwidth;            ///< ADC resolution
    adc_atten_t attenuation;            ///< ADC attenuation
    float reference_voltage;            ///< Reference voltage for calculations
    bool is_configured;                 ///< Channel configuration status
} adc_hal_channel_config_t;

/**
 * @brief Shared ADC unit structure
 */
typedef struct {
    adc_oneshot_unit_handle_t handle;   ///< ESP-IDF ADC handle
    adc_unit_t unit;                    ///< ADC unit
    int ref_count;                      ///< Reference counter
    adc_hal_channel_config_t channels[ADC_HAL_MAX_CHANNELS]; ///< Channel configurations
    bool is_initialized;                ///< Initialization status
} adc_hal_shared_unit_t;

/**
 * @brief Initialize shared ADC unit
 * 
 * @param unit ADC unit to initialize (ADC_UNIT_1 or ADC_UNIT_2)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_shared_init(adc_unit_t unit);

/**
 * @brief Deinitialize shared ADC unit
 * 
 * @param unit ADC unit to deinitialize
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_shared_deinit(adc_unit_t unit);

/**
 * @brief Add and configure a channel to shared ADC unit
 * 
 * @param unit ADC unit
 * @param channel ADC channel to add
 * @param bitwidth ADC resolution
 * @param attenuation ADC attenuation
 * @param reference_voltage Reference voltage for voltage calculations
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_shared_add_channel(adc_unit_t unit, adc_channel_t channel, 
                                     adc_bitwidth_t bitwidth, adc_atten_t attenuation, 
                                     float reference_voltage);

/**
 * @brief Read raw value from shared ADC channel
 * 
 * @param unit ADC unit
 * @param channel ADC channel to read
 * @param raw_value Pointer to store raw ADC value
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_shared_read_raw(adc_unit_t unit, adc_channel_t channel, int* raw_value);

/**
 * @brief Read voltage from shared ADC channel
 * 
 * @param unit ADC unit
 * @param channel ADC channel to read
 * @param voltage Pointer to store voltage value
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_shared_read_voltage(adc_unit_t unit, adc_channel_t channel, float* voltage);

/**
 * @brief Remove channel from shared ADC unit
 * 
 * @param unit ADC unit
 * @param channel ADC channel to remove
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t adc_hal_shared_remove_channel(adc_unit_t unit, adc_channel_t channel);

/**
 * @brief Check if shared ADC unit is initialized
 * 
 * @param unit ADC unit to check
 * @return true if initialized, false otherwise
 */
bool adc_hal_shared_is_initialized(adc_unit_t unit);

#endif // ADC_HAL_H