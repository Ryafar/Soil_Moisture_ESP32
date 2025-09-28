/**
 * @file soil_monitor_app.h
 * @brief Soil Moisture Monitoring Application
 * 
 * This application demonstrates the usage of the CSM V2 driver to create
 * a soil moisture monitoring system.
 */

#ifndef SOIL_MONITOR_APP_H
#define SOIL_MONITOR_APP_H

#include "esp_err.h"
#include "../drivers/csm_v2_driver/csm_v2_driver.h"
#include "../drivers/wifi/wifi_manager.h"
#include "../drivers/http/http_client.h"

/**
 * @brief Application configuration
 */
typedef struct {
    adc_unit_t adc_unit;                ///< ADC unit to use
    adc_channel_t adc_channel;          ///< ADC channel to use
    uint32_t measurement_interval_ms;   ///< Measurement interval in milliseconds
    bool enable_logging;                ///< Enable detailed logging
    float dry_calibration_voltage;      ///< Dry calibration voltage
    float wet_calibration_voltage;      ///< Wet calibration voltage
    bool enable_wifi;                   ///< Enable WiFi connectivity
    bool enable_http_sending;           ///< Enable HTTP data transmission
    char device_id[32];                 ///< Unique device identifier
} soil_monitor_config_t;

/**
 * @brief Application handle
 */
typedef struct {
    csm_v2_driver_t sensor_driver;      ///< Soil sensor driver
    soil_monitor_config_t config;       ///< Application configuration
    bool is_running;                    ///< Application running status
} soil_monitor_app_t;

/**
 * @brief Get default application configuration
 * 
 * @param config Pointer to configuration structure to fill
 */
void soil_monitor_get_default_config(soil_monitor_config_t* config);

/**
 * @brief Initialize the soil monitoring application
 * 
 * @param app Pointer to application handle
 * @param config Pointer to application configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t soil_monitor_init(soil_monitor_app_t* app, const soil_monitor_config_t* config);

/**
 * @brief Start the soil monitoring application
 * 
 * @param app Pointer to application handle
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t soil_monitor_start(soil_monitor_app_t* app);

/**
 * @brief Stop the soil monitoring application
 * 
 * @param app Pointer to application handle
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t soil_monitor_stop(soil_monitor_app_t* app);

/**
 * @brief Deinitialize the soil monitoring application
 * 
 * @param app Pointer to application handle
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t soil_monitor_deinit(soil_monitor_app_t* app);

/**
 * @brief Perform calibration sequence
 * 
 * @param app Pointer to application handle
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t soil_monitor_calibrate(soil_monitor_app_t* app);

#endif // SOIL_MONITOR_APP_H