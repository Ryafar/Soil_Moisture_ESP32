#include "battery_monitor.h"
#include "../config/esp32-config.h"
#include "../drivers/adc/adc_manager.h"
#include "esp_log.h"
#include "esp_err.h"

static const char* TAG = "BATTERY_MONITOR";





// MARK: init
/**
 * @brief Initialize battery monitoring (init ADC, etc.)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_monitor_init() {
    
    // Initialize shared ADC unit
    esp_err_t ret = adc_shared_init(BATTERY_ADC_UNIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize shared ADC unit for battery monitoring: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Add battery monitoring channel to shared ADC
    ret = adc_shared_add_channel(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, 
                                     BATTERY_ADC_BITWIDTH, BATTERY_ADC_ATTENUATION, 
                                     BATTERY_ADC_VREF);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add battery channel to shared ADC: %s", esp_err_to_name(ret));
        adc_shared_deinit(BATTERY_ADC_UNIT);
        return ret;
    }

    ESP_LOGI(TAG, "Battery monitor initialized on ADC%d CH%d", BATTERY_ADC_UNIT + 1, BATTERY_ADC_CHANNEL);
    return ESP_OK;
}



// MARK: deinit
/**
 * @brief Deinitialize battery monitoring
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_monitor_deinit() {
    // Remove battery channel from shared ADC
    esp_err_t ret = adc_shared_remove_channel(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove battery channel from shared ADC: %s", esp_err_to_name(ret));
    }
    
    // Deinitialize shared ADC unit (will only actually deinit if ref count reaches 0)
    ret = adc_shared_deinit(BATTERY_ADC_UNIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize shared ADC for battery monitoring: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Battery monitor deinitialized");
    return ESP_OK;
}




// MARK: measure
/**
 * @brief Measure battery voltage and return the measurement
 * @param voltage Pointer to store the measured voltage
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_monitor_measure(float* voltage) {
    if (voltage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float raw_voltage = 0.0f;
    esp_err_t ret = adc_shared_read_voltage(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, &raw_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read battery voltage: %s", esp_err_to_name(ret));
        return ret;
    }

    // Apply voltage scale factor (for voltage divider)
    *voltage = raw_voltage * BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR;
    return ESP_OK;
}
