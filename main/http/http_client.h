/**
 * @file http_client.h
 * @brief HTTP Client for Soil Moisture Data Transmission
 * 
 * This module handles HTTP communication to send soil moisture sensor
 * data to a remote server (your PC).
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "esp_err.h"
#include "../csm-v2-driver/csm-v2-driver.h"

/**
 * @brief HTTP client configuration
 */
typedef struct {
    char server_ip[16];     ///< Server IP address
    int server_port;        ///< Server port
    char endpoint[64];      ///< API endpoint path
    int timeout_ms;         ///< Request timeout in milliseconds
    int max_retries;        ///< Maximum retry attempts
} http_client_config_t;

/**
 * @brief Soil sensor data packet for transmission
 */
typedef struct {
    uint64_t timestamp;             ///< Unix timestamp
    float voltage;                  ///< Raw voltage reading
    float moisture_percent;         ///< Moisture percentage
    int raw_adc;                    ///< Raw ADC value
    char device_id[32];            ///< Device identifier
} soil_data_packet_t;

/**
 * @brief HTTP response status
 */
typedef enum {
    HTTP_RESPONSE_OK = 0,
    HTTP_RESPONSE_ERROR,
    HTTP_RESPONSE_TIMEOUT,
    HTTP_RESPONSE_NO_CONNECTION
} http_response_status_t;

/**
 * @brief Initialize HTTP client
 * 
 * @param config HTTP client configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t http_client_init(const http_client_config_t* config);

/**
 * @brief Deinitialize HTTP client
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t http_client_deinit(void);

/**
 * @brief Send soil moisture data to server
 * 
 * @param reading Sensor reading to send
 * @param device_id Device identifier string
 * @return http_response_status_t Response status
 */
http_response_status_t http_client_send_soil_data(const csm_v2_reading_t* reading, const char* device_id);

/**
 * @brief Send raw data packet to server
 * 
 * @param packet Data packet to send
 * @return http_response_status_t Response status
 */
http_response_status_t http_client_send_data_packet(const soil_data_packet_t* packet);

/**
 * @brief Test HTTP connection to server
 * 
 * @return http_response_status_t Connection status
 */
http_response_status_t http_client_test_connection(void);

/**
 * @brief Get the last HTTP status code
 * 
 * @return int HTTP status code from last request
 */
int http_client_get_last_status_code(void);

#endif // HTTP_CLIENT_H