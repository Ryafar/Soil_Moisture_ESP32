/**
 * @file espnow.h
 * @brief ESP-NOW Driver - Low-level communication layer
 *
 * Generic ESP-NOW driver that handles peer management, sending/receiving with ACK,
 * and channel management. Independent of application-specific data structures.
 */

#ifndef ESPNOW_DRIVER_H
#define ESPNOW_DRIVER_H

#include "esp_err.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include <stdint.h>
#include <stdbool.h>

#define ESPNOW_MAX_DATA_LEN        250  ///< ESP-NOW maximum data length
#define ESPNOW_ACK_TIMEOUT_MS      1000 ///< Timeout waiting for ACK
#define ESPNOW_BROADCAST_MAC       {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

/**
 * @brief ESP-NOW message types
 */
typedef enum {
    ESPNOW_MSG_TYPE_DATA = 0,    ///< Data message
    ESPNOW_MSG_TYPE_ACK  = 1     ///< ACK message
} espnow_msg_type_t;

/**
 * @brief ESP-NOW send status
 */
typedef enum {
    ESPNOW_SEND_SUCCESS = 0,
    ESPNOW_SEND_FAIL,
    ESPNOW_SEND_NO_ACK,
    ESPNOW_SEND_TIMEOUT
} espnow_send_status_t;

/**
 * @brief ESP-NOW receive callback function type
 * 
 * @param mac_addr Source MAC address
 * @param data Received data
 * @param len Data length
 */
typedef void (*espnow_recv_cb_t)(const uint8_t *mac_addr, const uint8_t *data, int len);

/**
 * @brief Initialize ESP-NOW
 * 
 * Must be called after WiFi is initialized.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_init(void);

/**
 * @brief Deinitialize ESP-NOW
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_deinit(void);

/**
 * @brief Initialize WiFi in STA mode for ESP-NOW usage
 * 
 * @param channel WiFi channel (1-13)
 * @param tx_power_dbm TX power in dBm (0 = use default, max ~20 dBm)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_init_wifi(uint8_t channel, int8_t tx_power_dbm);

/**
 * @brief Add a peer to ESP-NOW
 * 
 * @param peer_mac Peer MAC address (6 bytes)
 * @param channel Peer's WiFi channel
 * @param encrypt Whether to use encryption (usually false for simplicity)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_add_peer(const uint8_t *peer_mac, uint8_t channel, bool encrypt);

/**
 * @brief Remove a peer from ESP-NOW
 * 
 * @param peer_mac Peer MAC address (6 bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_remove_peer(const uint8_t *peer_mac);

/**
 * @brief Check if a peer exists
 * 
 * @param peer_mac Peer MAC address (6 bytes)
 * @return true if peer exists, false otherwise
 */
bool espnow_peer_exists(const uint8_t *peer_mac);

/**
 * @brief Send data via ESP-NOW
 * 
 * @param dest_mac Destination MAC address (6 bytes, or NULL for broadcast)
 * @param data Data to send
 * @param len Data length (max ESPNOW_MAX_DATA_LEN)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_send(const uint8_t *dest_mac, const uint8_t *data, size_t len);

/**
 * @brief Send data and wait for ACK
 * 
 * @param dest_mac Destination MAC address (6 bytes)
 * @param data Data to send
 * @param len Data length
 * @param timeout_ms Timeout in milliseconds
 * @return espnow_send_status_t Send status
 */
espnow_send_status_t espnow_send_with_ack(const uint8_t *dest_mac, 
                                          const uint8_t *data, size_t len,
                                          uint32_t timeout_ms);

/**
 * @brief Register receive callback
 * 
 * @param cb Callback function
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_register_recv_callback(espnow_recv_cb_t cb);

/**
 * @brief Send an ACK message
 * 
 * @param dest_mac Destination MAC address (6 bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_send_ack(const uint8_t *dest_mac);

/**
 * @brief Set WiFi channel
 * 
 * @param channel Channel number (1-13)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_set_channel(uint8_t channel);

/**
 * @brief Get current WiFi channel
 * 
 * @return Current channel number
 */
uint8_t espnow_get_channel(void);

/**
 * @brief Get the MAC address of the last device that sent an ACK
 * 
 * Useful for hub discovery when broadcasting packets. Captures the sender's
 * MAC address from the most recent ACK reception.
 * 
 * @param mac_addr Output buffer (must be 6 bytes)
 * @return ESP_OK on success
 */
esp_err_t espnow_get_ack_responder_mac(uint8_t *mac_addr);

#endif // ESPNOW_DRIVER_H