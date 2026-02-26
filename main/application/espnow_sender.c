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

// MARK: init
esp_err_t espnow_sender_init(const espnow_sender_config_t *config,
                             uint8_t initial_channel, int8_t tx_power_dbm)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    // Store configuration
    memcpy(&s_config, config, sizeof(espnow_sender_config_t));

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

    // Add hub as peer only if not in discovery mode (hub_mac != all zeros)
    bool is_discovery = (s_config.hub_mac[0] == 0xff && s_config.hub_mac[1] == 0xff &&
                        s_config.hub_mac[2] == 0xff && s_config.hub_mac[3] == 0xff &&
                        s_config.hub_mac[4] == 0xff && s_config.hub_mac[5] == 0xff);
    
    if (is_discovery) {
        initial_channel = 0; // Start with channel 0 for discovery (will scan all channels)
    }

    // Add peer
    err = espnow_add_peer(s_config.hub_mac, 0, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Add broadcast peer failed: %s", esp_err_to_name(err));
        espnow_deinit();
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW sender initialized (hub: " MACSTR ", ch=%d)",
             MAC2STR(s_config.hub_mac), initial_channel);
    return ESP_OK;
}

esp_err_t espnow_sender_init_on_existing_wifi(const espnow_sender_config_t *config,
                                               uint8_t wifi_channel)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_is_connected = true; // Assume WiFi is already connected when using this init

    // Store configuration
    memcpy(&s_config, config, sizeof(espnow_sender_config_t));

    // Initialize ESP-NOW (WiFi must already be initialized)
    esp_err_t err = espnow_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(err));
        return err;
    }

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

espnow_sender_status_t espnow_set_espnow_channel(uint8_t channel)
{

    esp_err_t err = espnow_set_channel(channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set channel failed: %s", esp_err_to_name(err));
        return ESPNOW_SENDER_ERROR;
    }
    return ESPNOW_SENDER_OK;
}

espnow_sender_status_t espnow_sender_send_data(const espnow_sensor_data_t *data,
                                                uint8_t *best_channel,
                                                uint8_t *ack_responder_mac)
{
    if (!s_initialized || !data || !best_channel || !ack_responder_mac) {
        ESP_LOGE(TAG, "Not initialized or invalid params");
        return ESPNOW_SENDER_ERROR;
    }
    
    // Clear responder MAC output
    memset(ack_responder_mac, 0, 6);
    
    // Determine target MAC: use hub_mac if set, otherwise broadcast for discovery
    uint8_t target_mac[6];
    bool is_discovery_mode = false;
    
    // Check if hub_mac is all zeros (discovery mode)
    if (s_config.hub_mac[0] == 0 && s_config.hub_mac[1] == 0 && 
        s_config.hub_mac[2] == 0 && s_config.hub_mac[3] == 0 &&
        s_config.hub_mac[4] == 0 && s_config.hub_mac[5] == 0) {
        // Broadcast to all devices
        memset(target_mac, 0xff, 6);
        is_discovery_mode = true;
        ESP_LOGI(TAG, "Hub MAC not configured, using broadcast mode for discovery");
    } else {
        memcpy(target_mac, s_config.hub_mac, 6);
        ESP_LOGI(TAG, "Sending to known hub: " MACSTR, MAC2STR(target_mac));
    }

    // If WiFi is connected, we cannot change channels - try only the WiFi's current channel
    if (wifi_is_connected) {
        // Query WiFi's current channel (WiFi locks the channel)
        uint8_t current_channel = 1;
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            current_channel = ap_info.primary;
            ESP_LOGI(TAG, "WiFi connected on channel %d", current_channel);
        } else {
            ESP_LOGE(TAG, "Failed to get WiFi AP info, using default channel 1");
        }
        ESP_LOGI(TAG, "WiFi connected, trying WiFi channel %d only", current_channel);
        
        for (uint8_t retry = 0; retry < s_config.max_retries; retry++) {
            espnow_send_status_t status = espnow_send_with_ack(
                target_mac,
                (const uint8_t *)data,
                sizeof(espnow_sensor_data_t),
                s_config.ack_timeout_ms
            );

            if (status == ESPNOW_SEND_SUCCESS) {
                *best_channel = current_channel;
                // Get the MAC of the device that sent ACK (for discovery mode)
                espnow_get_ack_responder_mac(ack_responder_mac);
                return ESPNOW_SENDER_OK;
            }

            if (retry < s_config.max_retries - 1) {
                vTaskDelay(pdMS_TO_TICKS(s_config.retry_delay_ms));
            }
        }

        ESP_LOGW(TAG, "No ACK received (WiFi connected, cannot scan other channels)");
        return ESPNOW_SENDER_NO_ACK;
    }

    // WiFi not connected - can scan all channels
    uint8_t current_channel = espnow_get_channel();
    
    // Try current channel first
    ESP_LOGI(TAG, "Attempting send on current channel %d", current_channel);
    for (uint8_t retry = 0; retry < s_config.max_retries; retry++) {
        espnow_send_status_t status = espnow_send_with_ack(
            target_mac,
            (const uint8_t *)data,
            sizeof(espnow_sensor_data_t),
            s_config.ack_timeout_ms
        );

        if (status == ESPNOW_SEND_SUCCESS) {
            *best_channel = current_channel;
            // Get the MAC of the device that sent ACK (for discovery mode)
            espnow_get_ack_responder_mac(ack_responder_mac);
            return ESPNOW_SENDER_OK;
        }

        if (retry < s_config.max_retries - 1) {
            vTaskDelay(pdMS_TO_TICKS(s_config.retry_delay_ms));
        }
    }

    // No ACK on current channel - scan all channels
    ESP_LOGW(TAG, "No ACK on channel %d, scanning all channels...", current_channel);

    for (uint8_t ch = 1; ch <= 13; ch++) {

        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

        // if hub mac is known, update peer for this channel before sending
        if (!is_discovery_mode) {
            // Remove old peer first
            espnow_remove_peer(s_config.hub_mac);
            // Add peer on new channel
            esp_err_t err = espnow_add_peer(s_config.hub_mac, ch, false);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Add peer failed on channel %d: %s", ch, esp_err_to_name(err));
                continue; // Try next channel
            }
        }

        ESP_LOGI(TAG, "Trying channel %d", ch);

        // Try sending on this channel
        for (uint8_t retry = 0; retry < s_config.max_retries; retry++) {
            espnow_send_status_t status = espnow_send_with_ack(
                target_mac,
                (const uint8_t *)data,
                sizeof(espnow_sensor_data_t),
                s_config.ack_timeout_ms
            );

            if (status == ESPNOW_SEND_SUCCESS) {
                *best_channel = ch;
                // Get the MAC of the device that sent ACK (for discovery mode)
                espnow_get_ack_responder_mac(ack_responder_mac);
                return ESPNOW_SENDER_OK;
            }

            if (retry < s_config.max_retries - 1) {
                vTaskDelay(pdMS_TO_TICKS(s_config.retry_delay_ms));
            }
        }
    }

    ESP_LOGE(TAG, "Failed to send data on all channels");
    return ESPNOW_SENDER_ALL_CHANNELS_FAILED;
}

esp_err_t espnow_sender_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    // Only remove peer if it was added
    bool is_discovery = (s_config.hub_mac[0] == 0 && s_config.hub_mac[1] == 0 &&
                        s_config.hub_mac[2] == 0 && s_config.hub_mac[3] == 0 &&
                        s_config.hub_mac[4] == 0 && s_config.hub_mac[5] == 0);
    
    if (is_discovery) {
        // Remove broadcast peer
        uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        espnow_remove_peer(broadcast_mac);
    } else {
        espnow_remove_peer(s_config.hub_mac);
    }
    
    espnow_deinit();
    
    s_initialized = false;
    ESP_LOGI(TAG, "ESP-NOW sender deinitialized");
    return ESP_OK;
}

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
