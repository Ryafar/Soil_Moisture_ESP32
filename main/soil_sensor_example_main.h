/**
 * @file soil-sensor-example-main.h
 * @brief Main application header for Soil Moisture Sensor project
 */

#ifndef SOIL_SENSOR_EXAMPLE_MAIN_H
#define SOIL_SENSOR_EXAMPLE_MAIN_H

#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"

// Project configuration
#include "config/esp32-config.h"

// Application layer
#include "application/soil_monitor_app.h"
#include "application/battery_monitor_task.h"

/**
 * @brief Main application entry point
 */
void app_main(void);

#endif // SOIL_SENSOR_EXAMPLE_MAIN_H


