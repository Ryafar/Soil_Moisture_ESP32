/**
 * @file espnow_sender.h
 * @brief ESP-NOW Sender - Application Layer
 *
 * Handles transmission of soil moisture and battery data via ESP-NOW with
 * automatic channel scanning and ACK verification. Compatible with hub receiver
 * that forwards data to InfluxDB/MQTT.
 */

#ifndef ESPNOW_SENDER_H
#define ESPNOW_SENDER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Sensor data packet for ESP-NOW transmission
 * 
 * This structure is designed to be compatible with both InfluxDB and MQTT
 * formats used by the hub receiver.
 */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;               ///< Message type (always DATA = 0)
    uint64_t timestamp_ms;          ///< Timestamp in milliseconds
    char device_id[32];             ///< Device identifier (from MAC)
    
    // Soil moisture data
    float soil_voltage;             ///< Soil sensor voltage
    float soil_moisture_percent;    ///< Moisture percentage
    int soil_raw_adc;               ///< Raw ADC reading
    
    // Battery data
    float battery_voltage;          ///< Battery voltage
    float battery_percentage;       ///< Battery percentage (0-100)
} espnow_sensor_data_t;

/**
 * @brief ESP-NOW sender configuration
 */
typedef struct {
    uint8_t hub_mac[6];             ///< Hub MAC address
    uint8_t start_channel;          ///< Starting channel for scanning
    uint8_t max_retries;            ///< Max retries per channel
    uint32_t retry_delay_ms;        ///< Delay between retries
    uint32_t ack_timeout_ms;        ///< ACK timeout
} espnow_sender_config_t;

/**
 * @brief ESP-NOW sender status
 */
typedef enum {
    ESPNOW_SENDER_OK = 0,
    ESPNOW_SENDER_NO_ACK,
    ESPNOW_SENDER_ALL_CHANNELS_FAILED,
    ESPNOW_SENDER_ERROR
} espnow_sender_status_t;

/**
 * @brief Initialize ESP-NOW sender
 * 
 * Initializes WiFi and ESP-NOW for sending. The channel parameter is typically
 * loaded from NVS (last successful channel).
 * 
 * @param config Sender configuration
 * @param initial_channel Initial WiFi channel to use
 * @param tx_power_dbm TX power in dBm (0 = default)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_sender_init(const espnow_sender_config_t *config,
                             uint8_t initial_channel, int8_t tx_power_dbm);

/**
 * @brief Initialize ESP-NOW sender on existing WiFi
 * 
 * Initializes ESP-NOW without initializing WiFi. Use this when WiFi is already
 * initialized (e.g., for a hub that needs both WiFi and ESP-NOW).
 * 
 * @param config Sender configuration
 * @param initial_channel Initial WiFi channel (must match current WiFi channel)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_sender_init_on_existing_wifi(const espnow_sender_config_t *config,
                                               uint8_t initial_channel);

/**
 * @brief Send sensor data with automatic channel scanning
 * 
 * Attempts to send data on the current channel. If no ACK is received,
 * scans through all WiFi channels (1-13) to find the hub. When successful,
 * the channel is returned via best_channel for saving to NVS.
 * 
 * If hub_mac is broadcast (all zeros), the MAC of the ACK responder is
 * returned via ack_responder_mac for hub discovery.
 * 
 * @param data Sensor data to send
 * @param best_channel Output: channel where ACK was received (or 0 if failed)
 * @param ack_responder_mac Output: MAC address of the device that sent ACK (for discovery)
 * @return espnow_sender_status_t Send status
 */
espnow_sender_status_t espnow_sender_send_data(const espnow_sensor_data_t *data,
                                                uint8_t *best_channel,
                                                uint8_t *ack_responder_mac);

/**
 * @brief Deinitialize ESP-NOW sender
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_sender_deinit(void);

/**
 * @brief Set ESP-NOW channel
 * 
 * @param channel WiFi channel to set
 * @return espnow_sender_status_t Status of the operation
 */
espnow_sender_status_t espnow_set_espnow_channel(uint8_t channel);

/**
 * @brief Build sensor data packet from individual measurements
 * 
 * Helper function to construct the packet structure.
 * 
 * @param packet Output packet
 * @param device_id Device identifier string
 * @param timestamp_ms Timestamp in milliseconds
 * @param soil_voltage Soil sensor voltage
 * @param soil_moisture_percent Moisture percentage
 * @param soil_raw_adc Raw ADC value
 * @param battery_voltage Battery voltage
 * @param battery_percentage Battery percentage
 */
void espnow_sender_build_packet(espnow_sensor_data_t *packet,
                                const char *device_id,
                                uint64_t timestamp_ms,
                                float soil_voltage,
                                float soil_moisture_percent,
                                int soil_raw_adc,
                                float battery_voltage,
                                float battery_percentage);

/**
 * @brief Check if MAC address is broadcast (discovery mode)
 * 
 * Checks if the MAC is 0xFF:FF:FF:FF:FF:FF, indicating discovery/broadcast mode.
 * 
 * @param mac MAC address (6 bytes)
 * @return true if broadcast MAC, false otherwise
 */
bool espnow_sender_is_broadcast_mac(const uint8_t *mac);

/**
 * @brief Check if MAC address is valid (not all zeros)
 * 
 * @param mac MAC address (6 bytes)
 * @return true if valid, false if all zeros
 */
bool espnow_sender_is_mac_valid(const uint8_t *mac);

#endif // ESPNOW_SENDER_H
