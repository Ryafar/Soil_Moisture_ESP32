#ifndef BATTERY_MONITOR_TASK_H
#define BATTERY_MONITOR_TASK_H

#include "esp_err.h"

#include "../drivers/adc/adc_manager.h"
#include "../drivers/led/led.h"
#include "../config/esp32-config.h"

/**
 * @brief Initialize battery monitoring (init ADC, etc.)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_monitor_init();

/**
 * @brief Deinitialize battery monitoring
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_monitor_deinit();

/**
 * @brief Measure battery voltage and return the measurement
 * @param voltage Pointer to store the measured voltage
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_monitor_measure(float* voltage);

#endif // BATTERY_MONITOR_TASK_H