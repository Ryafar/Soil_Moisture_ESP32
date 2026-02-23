/**
 * @file influxdb_client.h
 * @brief InfluxDB Client for Time-Series Data Storage
 * 
 * This module handles InfluxDB line protocol communication to store
 * soil moisture and battery sensor data.
 */

#ifndef INFLUXDB_CLIENT_H
#define INFLUXDB_CLIENT_H

#include "../../utils/esp_utils.h"
#include "../../config/esp32-config.h"
#include "../../config/credentials.h"

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief InfluxDB client configuration
 */
typedef struct {
    char server[64];            ///< InfluxDB server hostname
    int port;                   ///< InfluxDB server port
    char bucket[32];            ///< InfluxDB bucket name
    char org[32];               ///< InfluxDB organization name
    char token[256];            ///< InfluxDB authentication token
    char endpoint[64];          ///< API endpoint path
    int timeout_ms;             ///< Request timeout in milliseconds
    int max_retries;            ///< Maximum retry attempts
} influxdb_client_config_t;

/**
 * @brief InfluxDB response status
 */
typedef enum {
    INFLUXDB_RESPONSE_OK = 0,
    INFLUXDB_RESPONSE_ERROR,
    INFLUXDB_RESPONSE_TIMEOUT,
    INFLUXDB_RESPONSE_NO_CONNECTION,
    INFLUXDB_RESPONSE_AUTH_ERROR
} influxdb_response_status_t;

/**
 * @brief Soil moisture measurement data
 */
typedef struct {
    uint64_t timestamp_ns;          ///< Timestamp in nanoseconds
    float voltage;                  ///< Sensor voltage
    float moisture_percent;         ///< Moisture percentage
    int raw_adc;                   ///< Raw ADC reading
    char device_id[32];            ///< Device identifier
} influxdb_soil_data_t;

/**
 * @brief Battery measurement data
 */
typedef struct {
    uint64_t timestamp_ns;          ///< Timestamp in nanoseconds
    float voltage;                  ///< Battery voltage
    float percentage;               ///< Battery percentage (if available)
    char device_id[32];            ///< Device identifier
} influxdb_battery_data_t;

/**
 * @brief Initialize InfluxDB client
 * 
 * @param config InfluxDB client configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t influxdb_client_init(const influxdb_client_config_t* config);

/**
 * @brief Deinitialize InfluxDB client
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t influxdb_client_deinit(void);

/**
 * @brief Test InfluxDB connection
 * 
 * @return influxdb_response_status_t Connection status
 */
influxdb_response_status_t influxdb_test_connection(void);

/**
 * @brief Get the last HTTP status code
 * 
 * @return int HTTP status code from last request
 */
int influxdb_get_last_status_code(void);

/**
 * @brief Send a raw InfluxDB line protocol string (for testing/debug)
 */
esp_err_t influxdb_send_line_protocol(const char* line_protocol);

/**
 * @brief Query whether the InfluxDB client has been initialized
 *
 * @return true if initialized, false otherwise
 */
bool influxdb_client_is_initialized(void);

#endif // INFLUXDB_CLIENT_H