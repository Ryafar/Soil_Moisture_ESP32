#ifndef BATTERY_MONITOR_TASK_H
#define BATTERY_MONITOR_TASK_H

#include "esp_err.h"

#include "../drivers/hal/adc_hal.h"
#include "../drivers/led/led.h"
#include "../config/esp32-config.h"

esp_err_t battery_monitor_init();
esp_err_t battery_monitor_deinit();
esp_err_t battery_monitor_read_voltage(float* voltage);
esp_err_t battery_monitor_start();
esp_err_t battery_monitor_stop();
void battery_monitor_task(void* pvParameters);


#endif // BATTERY_MONITOR_TASK_H