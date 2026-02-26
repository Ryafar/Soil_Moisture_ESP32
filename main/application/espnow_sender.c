/**
 * @file espnow_sender.c
 * @brief ESP-NOW Sender - Implementation
 *
 * Application layer for sending sensor data via ESP-NOW with channel scanning.
 */

#include "espnow_sender.h"
#include "../drivers/espnow/espnow.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ESPNOW_SENDER";

// Configuration
static espnow_sender_config_t s_config = {0};
static bool s_initialized = false;
static bool wifi_is_connected = false;

// MARK: Helper Functions

/**
 * @brief Check if MAC address is broadcast address (0xFF:FF:FF:FF:FF:FF)
 */
static bool is_broadcast_mac(const uint8_t *mac)
{
    return (mac[0] == 0xff && mac[1] == 0xff && mac[2] == 0xff &&
            mac[3] == 0xff && mac[4] == 0xff && mac[5] == 0xff);
}

/**
 * @brief Check if MAC address is all zeros (0x00:00:00:00:00:00)
 */
static bool is_zero_mac(const uint8_t *mac)
{
    return (mac[0] == 0 && mac[1] == 0 && mac[2] == 0 &&
            mac[3] == 0 && mac[4] == 0 && mac[5] == 0);
}

/**
 * @brief Check if MAC address is valid (not all zeros)
 */
static bool is_mac_valid(const uint8_t *mac)
{
    return !is_zero_mac(mac);
}

/**
 * @brief Try to send data on current channel with retries
 * 
 * @param target_mac Destination MAC address
 * @param data Data to send
 * @param data_len Data length
 * @return true if successful, false otherwise
 */
static bool try_send_with_retries(const uint8_t *target_mac, 
                                  const uint8_t *data, 
                                  size_t data_len)
{
    for (uint8_t retry = 0; retry < s_config.max_retries; retry++) {
        espnow_send_status_t status = espnow_send_with_ack(
            target_mac, data, data_len, s_config.ack_timeout_ms);

        if (status == ESPNOW_SEND_SUCCESS) {
            return true;
        }

        if (retry < s_config.max_retries - 1) {
            vTaskDelay(pdMS_TO_TICKS(s_config.retry_delay_ms));
        }
    }
    
    return false;
}

/**
 * @brief Get WiFi's current channel (when WiFi is connected)
 * 
 * @param channel Output: current WiFi channel
 * @return true if successful, false otherwise
 */
static bool get_wifi_channel(uint8_t *channel)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        *channel = ap_info.primary;
        return true;
    }
    return false;
}

/**
 * @brief Update peer for a specific channel (removes and re-adds peer)
 * 
 * @param peer_mac Peer MAC address
 * @param channel New channel
 * @return ESP_OK on success
 */
static esp_err_t update_peer_channel(const uint8_t *peer_mac, uint8_t channel)
{
    espnow_remove_peer(peer_mac);
    return espnow_add_peer(peer_mac, channel, false);
}

// MARK: Initialization

esp_err_t espnow_sender_init(const espnow_sender_config_t *config,
                             uint8_t initial_channel, int8_t tx_power_dbm)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    // Store configuration
    memcpy(&s_config, config, sizeof(espnow_sender_config_t));
    wifi_is_connected = false;

    // Initialize WiFi for ESP-NOW
    esp_err_t err = espnow_init_wifi(initial_channel, tx_power_dbm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize ESP-NOW
    err = espnow_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Add peer (broadcast for discovery, or specific hub MAC)
    // Use channel 0 for broadcast (wildcard channel)
    uint8_t peer_channel = is_broadcast_mac(s_config.hub_mac) ? 0 : initial_channel;
    err = espnow_add_peer(s_config.hub_mac, peer_channel, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Add peer failed: %s", esp_err_to_name(err));
        espnow_deinit();
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW sender initialized (hub: " MACSTR ", ch=%d, mode=%s)",
             MAC2STR(s_config.hub_mac), initial_channel,
             is_broadcast_mac(s_config.hub_mac) ? "discovery" : "unicast");
    return ESP_OK;
}

esp_err_t espnow_sender_init_on_existing_wifi(const espnow_sender_config_t *config,
                                               uint8_t wifi_channel)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    // Store configuration
    memcpy(&s_config, config, sizeof(espnow_sender_config_t));
    wifi_is_connected = true;

    // Initialize ESP-NOW (WiFi must already be initialized)
    esp_err_t err = espnow_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Add peer
    err = espnow_add_peer(s_config.hub_mac, wifi_channel, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Add peer failed: %s", esp_err_to_name(err));
        espnow_deinit();
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW sender initialized on existing WiFi (hub: " MACSTR ", ch=%d)",
             MAC2STR(s_config.hub_mac), wifi_channel);
    return ESP_OK;
}

// MARK: Channel Scanning

/**
 * @brief Try sending on a single WiFi channel (WiFi-connected mode)
 * 
 * @param target_mac Target MAC address
 * @param data Data to send
 * @param data_len Data length
 * @param channel Output: channel used
 * @return true if successful, false otherwise
 */
static bool try_send_on_wifi_channel(const uint8_t *target_mac,
                                     const uint8_t *data,
                                     size_t data_len,
                                     uint8_t *channel)
{
    // Get WiFi's current channel (WiFi locks the channel)
    if (!get_wifi_channel(channel)) {
        ESP_LOGE(TAG, "Failed to get WiFi channel");
        *channel = 1;  // Fallback to default
    }

    ESP_LOGI(TAG, "WiFi connected on channel %d, trying WiFi channel only", *channel);
    
    if (try_send_with_retries(target_mac, data, data_len)) {
        return true;
    }

    ESP_LOGW(TAG, "No ACK on WiFi channel (cannot scan, WiFi is connected)");
    return false;
}

/**
 * @brief Scan all WiFi channels to find hub
 * 
 * @param target_mac Target MAC address
 * @param data Data to send
 * @param data_len Data length
 * @param is_discovery_mode Whether in discovery mode (affects peer management)
 * @param best_channel Output: channel where ACK was received
 * @return true if successful, false otherwise
 */
static bool scan_all_channels(const uint8_t *target_mac,
                              const uint8_t *data,
                              size_t data_len,
                              bool is_discovery_mode,
                              uint8_t *best_channel)
{
    ESP_LOGI(TAG, "Scanning all channels...");

    for (uint8_t ch = 1; ch <= 13; ch++) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

        // If hub MAC is known (unicast mode), update peer for this channel
        if (!is_discovery_mode) {
            esp_err_t err = update_peer_channel(s_config.hub_mac, ch);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to update peer on channel %d: %s", 
                        ch, esp_err_to_name(err));
                continue;
            }
        }

        ESP_LOGI(TAG, "Trying channel %d", ch);

        if (try_send_with_retries(target_mac, data, data_len)) {
            *best_channel = ch;
            return true;
        }
    }

    ESP_LOGE(TAG, "Failed to send data on all channels");
    return false;
}

// MARK: Send Data

espnow_sender_status_t espnow_sender_send_data(const espnow_sensor_data_t *data,
                                                uint8_t *best_channel,
                                                uint8_t *ack_responder_mac)
{
    if (!s_initialized || !data || !best_channel || !ack_responder_mac) {
        ESP_LOGE(TAG, "Not initialized or invalid params");
        return ESPNOW_SENDER_ERROR;
    }
    
    // Clear output parameters
    *best_channel = 0;
    memset(ack_responder_mac, 0, 6);
    
    // Determine target MAC and mode
    uint8_t target_mac[6];
    bool is_discovery_mode = is_broadcast_mac(s_config.hub_mac);
    
    if (is_discovery_mode) {
        memcpy(target_mac, s_config.hub_mac, 6);  // Use broadcast MAC
        ESP_LOGI(TAG, "Discovery mode: broadcasting to find hub");
    } else {
        memcpy(target_mac, s_config.hub_mac, 6);
        ESP_LOGI(TAG, "Unicast mode: sending to " MACSTR, MAC2STR(target_mac));
    }

    bool success = false;
    
    // WiFi connected: can only try WiFi's current channel
    if (wifi_is_connected) {
        success = try_send_on_wifi_channel(target_mac, 
                                          (const uint8_t *)data,
                                          sizeof(espnow_sensor_data_t),
                                          best_channel);
    }
    // WiFi not connected: try current channel first, then scan all
    else {
        uint8_t current_channel = espnow_get_channel();
        ESP_LOGI(TAG, "Trying current channel %d first", current_channel);
        
        // Try current channel
        if (try_send_with_retries(target_mac, (const uint8_t *)data, 
                                 sizeof(espnow_sensor_data_t))) {
            *best_channel = current_channel;
            success = true;
        }
        // No ACK - scan all channels
        else {
            ESP_LOGW(TAG, "No ACK on channel %d", current_channel);
            success = scan_all_channels(target_mac,
                                       (const uint8_t *)data,
                                       sizeof(espnow_sensor_data_t),
                                       is_discovery_mode,
                                       best_channel);
        }
    }

    // Handle result
    if (success) {
        // Get ACK responder MAC (for discovery)
        espnow_get_ack_responder_mac(ack_responder_mac);
        ESP_LOGI(TAG, "Data sent successfully on channel %d, ACK from " MACSTR,
                *best_channel, MAC2STR(ack_responder_mac));
        return ESPNOW_SENDER_OK;
    }

    return wifi_is_connected ? ESPNOW_SENDER_NO_ACK : ESPNOW_SENDER_ALL_CHANNELS_FAILED;
}

// MARK: Cleanup

esp_err_t espnow_sender_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    // Remove peer
    espnow_remove_peer(s_config.hub_mac);
    
    // Deinitialize ESP-NOW
    espnow_deinit();
    
    s_initialized = false;
    ESP_LOGI(TAG, "ESP-NOW sender deinitialized");
    return ESP_OK;
}

// MARK: Packet Building

void espnow_sender_build_packet(espnow_sensor_data_t *packet,
                                const char *device_id,
                                uint64_t timestamp_ms,
                                float soil_voltage,
                                float soil_moisture_percent,
                                int soil_raw_adc,
                                float battery_voltage,
                                float battery_percentage)
{
    if (!packet || !device_id) {
        return;
    }

    memset(packet, 0, sizeof(espnow_sensor_data_t));
    
    packet->msg_type = ESPNOW_MSG_TYPE_DATA;
    packet->timestamp_ms = timestamp_ms;
    strncpy(packet->device_id, device_id, sizeof(packet->device_id) - 1);
    
    packet->soil_voltage = soil_voltage;
    packet->soil_moisture_percent = soil_moisture_percent;
    packet->soil_raw_adc = soil_raw_adc;
    
    packet->battery_voltage = battery_voltage;
    packet->battery_percentage = battery_percentage;
}

// MARK: Public Helper Functions

bool espnow_sender_is_broadcast_mac(const uint8_t *mac)
{
    return is_broadcast_mac(mac);
}

bool espnow_sender_is_mac_valid(const uint8_t *mac)
{
    return is_mac_valid(mac);
}
