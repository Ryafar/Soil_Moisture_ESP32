/**
 * @file main.h
 * @brief Main application header for Battery Monitor
 */

#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"

// Project configuration
#include "config/esp32-config.h"

// Application layer
#include "application/battery_monitor.h"

/**
 * @brief Main application entry point
 */
void app_main(void);

#endif // MAIN_H


