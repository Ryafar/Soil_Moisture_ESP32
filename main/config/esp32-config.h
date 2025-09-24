/**
 * @file esp32-config.h
 * @brief ESP32 Hardware Configuration for Soil Moisture Sensor Project
 * 
 * This file contains all hardware-specific configurations including
 * pin assignments, ADC settings, and project constants.
 */

#ifndef ESP32_CONFIG_H
#define ESP32_CONFIG_H

#include "esp_adc/adc_oneshot.h"
#include "credentials.h"

// ============================================================================
// ADC Configuration
// ============================================================================

#define SOIL_ADC_UNIT           ADC_UNIT_1
#define SOIL_ADC_CHANNEL        ADC_CHANNEL_0
#define SOIL_ADC_BITWIDTH       ADC_BITWIDTH_12
#define SOIL_ADC_ATTENUATION    ADC_ATTEN_DB_11
#define SOIL_ADC_VREF           3.3f

// ============================================================================
// Sensor Configuration
// ============================================================================

#define SOIL_DRY_VOLTAGE_DEFAULT    3.0f
#define SOIL_WET_VOLTAGE_DEFAULT    1.0f
#define SOIL_MEASUREMENT_INTERVAL_MS    1000

// ============================================================================
// Task Configuration
// ============================================================================

#define SOIL_TASK_STACK_SIZE    4096
#define SOIL_TASK_PRIORITY      5
#define SOIL_TASK_NAME          "soil_monitor"

// ============================================================================
// Logging Configuration
// ============================================================================

#define SOIL_ENABLE_DETAILED_LOGGING    1
#define SOIL_LOG_LEVEL          ESP_LOG_INFO

// ============================================================================
// Calibration Configuration
// ============================================================================

#define SOIL_AUTO_CALIBRATION_ENABLE    0
#define SOIL_CALIBRATION_TIMEOUT_MS     10000
#define SOIL_CALIBRATION_SAMPLES        10

// ============================================================================
// WiFi Configuration
// ============================================================================

#define WIFI_MAX_RETRY          10
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1

// ============================================================================
// HTTP Configuration
// ============================================================================

#define HTTP_SERVER_IP          "192.168.1.13"    // Your PC's IP address
#define HTTP_SERVER_PORT        8080
#define HTTP_ENDPOINT           "/soil-data"
#define HTTP_TIMEOUT_MS         5000
#define HTTP_MAX_RETRIES        3

#endif // ESP32_CONFIG_H




