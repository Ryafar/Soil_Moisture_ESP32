/*
 * main.c - ESP32-C6 Moisture Sensor Project
 *
 * Author: Ryafar
 * Date: 2025-09-18
 * Description: Reads soil moisture data from a capacitive sensor using ADC.
 * Target: ESP32-C6
 * License: MIT
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "driver/adc.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "SOIL_SENSOR";

// Pick ADC1 channel: e.g. GPIO1 -> ADC1_CHANNEL_0
#define SOIL_ADC_CHANNEL ADC_CHANNEL_0

void soil_task(void *pvParameters) {
    adc_oneshot_unit_handle_t adc_handle;

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11,  // ~0–3.3V
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, SOIL_ADC_CHANNEL, &config));

    while (1) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, SOIL_ADC_CHANNEL, &raw));

        float voltage = (raw / 4095.0f) * 3.3f;
        ESP_LOGI(TAG, "Raw: %d   Voltage: %.2f V", raw, voltage);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    xTaskCreate(soil_task, "soil_task", 2048, NULL, 5, NULL);
}
