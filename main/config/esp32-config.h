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

// GPIO Pin Assignments
#define LED_GPIO_NUM           GPIO_NUM_22

// ============================================================================
// ADC Configuration
// ============================================================================

#define SOIL_ADC_MEASUREMENTS    5   // Number of ADC measurements to average for soil moisture reading
#define BATTERY_ADC_MEASUREMENTS 5   // Number of ADC measurements to average for battery

#define SOIL_ADC_UNIT           ADC_UNIT_1
#define SOIL_ADC_CHANNEL        ADC_CHANNEL_0
#define SOIL_ADC_BITWIDTH       ADC_BITWIDTH_12
#define SOIL_ADC_ATTENUATION    ADC_ATTEN_DB_12
#define SOIL_ADC_VREF           3.3f

#define BATTERY_ADC_UNIT        ADC_UNIT_1
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_3
#define BATTERY_ADC_BITWIDTH    ADC_BITWIDTH_12
#define BATTERY_ADC_ATTENUATION ADC_ATTEN_DB_12 // 0 - 2.45v range (suitable for voltage divider)
#define BATTERY_ADC_VREF        3.3f

// ============================================================================
// Task Configuration
// ============================================================================

#define SOIL_TASK_STACK_SIZE            1024
#define SOIL_TASK_PRIORITY              5
#define SOIL_TASK_NAME                  "soil_monitor"
#define SOIL_SENSOR_POWER_PIN           GPIO_NUM_19
#define SOIL_AUTO_CALIBRATION_ENABLE    0
#define SOIL_CALIBRATION_TIMEOUT_MS     10000
#define SOIL_CALIBRATION_SAMPLES        10
#define SOIL_DRY_VOLTAGE_DEFAULT        3.0f
#define SOIL_WET_VOLTAGE_DEFAULT        0.0f
#define SOIL_MEASUREMENT_INTERVAL_MS    10 * 1000
#define SOIL_MEASUREMENTS_PER_CYCLE     1  // Number of soil measurements before deep sleep

#define BATTERY_MONITOR_TASK_STACK_SIZE    1024
#define BATTERY_MONITOR_TASK_PRIORITY      5
#define BATTERY_MONITOR_TASK_NAME          "battery_monitor"
#define BATTERY_MONITOR_MEASUREMENT_INTERVAL_MS    10 * 1000
#define BATTERY_MONITOR_LOW_VOLTAGE_THRESHOLD      3.2f
#define BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR      2.0f  // 1-1 voltage divider
#define BATTERY_MONITOR_USE_DEEP_SLEEP_ON_LOW_BATTERY 1
#define BATTERY_MEASUREMENTS_PER_CYCLE  1  // Number of battery measurements before deep sleep

// ============================================================================
// Deep Sleep Configuration
// ============================================================================

#define DEEP_SLEEP_ENABLED              1                   // Enable/disable deep sleep mode
#define DEEP_SLEEP_DURATION_SECONDS     (60*60)           // 60 minutes in seconds
#define DEEP_SLEEP_WAKEUP_DELAY_MS      100                 // Delay before entering deep sleep

// ============================================================================
// NTP Time Synchronization Configuration
// ============================================================================

#define NTP_ENABLED                     1                   // Enable/disable NTP time synchronization (0 = use server time, 1 = use NTP time)
#define NTP_SYNC_TIMEOUT_MS             15000               // NTP sync timeout in milliseconds

// ============================================================================
// Logging Configuration
// ============================================================================

#define SOIL_ENABLE_DETAILED_LOGGING    1
#define SOIL_LOG_LEVEL          ESP_LOG_INFO

// ============================================================================
// WiFi Configuration
// ============================================================================

#define WIFI_MAX_RETRY          10
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1

// ============================================================================
// InfluxDB Configuration
// ============================================================================

#define USE_INFLUXDB            1                   // Enable InfluxDB data logging        
#define INFLUXDB_SERVER         "data.michipi.mywire.org"
#define INFLUXDB_PORT           443                 // HTTPS port (nginx reverse proxy)
#define INFLUXDB_USE_HTTPS      1                   // HTTPS required for nginx proxy
#define INFLUXDB_BUCKET         "soil-test"
#define INFLUXDB_ORG            "Michipi"           // Note: org is case-sensitive and must match InfluxDB exactly
#define INFLUXDB_ENDPOINT       "/api/v2/write"

#define HTTP_TIMEOUT_MS         15000               // Increased timeout to 15s
#define HTTP_MAX_RETRIES        3                   // More retries
#define HTTP_ENABLE_BUFFERING   1
#define HTTP_MAX_BUFFERED_PACKETS  100


// ============================================================================
// MQTT Configuration
// ============================================================================

#define USE_MQTT                1                   // Enable MQTT data publishing
#define MQTT_BROKER_URI         "mqtt://192.168.1.253:1883"  // MQTT broker URI (mqtt:// or mqtts://)
#define MQTT_BASE_TOPIC         "soil_sensor"       // Base topic for MQTT publishing
#define MQTT_CLIENT_ID_PREFIX   "esp32_soil_"       // Client ID prefix (will append device ID)
#define MQTT_KEEPALIVE          120                 // Keep-alive interval in seconds
#define MQTT_TIMEOUT_MS         10000               // Connection timeout in milliseconds
#define MQTT_USE_SSL            0                   // Use SSL/TLS (0 = no, 1 = yes)

#endif // ESP32_CONFIG_H




