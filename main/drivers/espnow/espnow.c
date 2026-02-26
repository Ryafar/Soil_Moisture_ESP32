/**
 * @file espnow.c
 * @brief ESP-NOW Driver - Implementation
 *
 * Generic ESP-NOW driver implementation with ACK support.
 */

#include "espnow.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ESPNOW_DRV";

// State variables
static uint8_t s_current_channel = 1;
static espnow_recv_cb_t s_user_recv_cb = NULL;
static SemaphoreHandle_t s_ack_semaphore = NULL;
static bool s_ack_received = false;
static uint8_t s_ack_responder_mac[6] = {0};  // MAC of device that sent ACK (for discovery)

/**
 * @brief Internal ESP-NOW receive callback
 */
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (len < 1) {
        return;
    }

    const uint8_t *mac_addr = recv_info->src_addr;
    
    // Check if this is an ACK message
    if (data[0] == ESPNOW_MSG_TYPE_ACK) {
        ESP_LOGD(TAG, "ACK received from " MACSTR, MAC2STR(mac_addr));
        s_ack_received = true;
        // Store the MAC of the device that sent the ACK (for discovery)
        memcpy(s_ack_responder_mac, mac_addr, 6);
        if (s_ack_semaphore) {
            xSemaphoreGive(s_ack_semaphore);
        }
        return;
    }
    
    // Pass data messages to user callback
    if (s_user_recv_cb) {
        s_user_recv_cb(mac_addr, data, len);
    }
}

esp_err_t espnow_init(void)
{

    // init wifi for esp-now (must be done before esp_now_init)
    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(s_current_channel, WIFI_SECOND_CHAN_NONE);

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Register receive callback
    err = esp_now_register_recv_cb(espnow_recv_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Register recv callback failed: %s", esp_err_to_name(err));
        esp_now_deinit();
        return err;
    }

    // Create ACK semaphore
    s_ack_semaphore = xSemaphoreCreateBinary();
    if (!s_ack_semaphore) {
        ESP_LOGE(TAG, "Failed to create ACK semaphore");
        esp_now_deinit();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "ESP-NOW initialized");
    return ESP_OK;
}

esp_err_t espnow_deinit(void)
{
    if (s_ack_semaphore) {
        vSemaphoreDelete(s_ack_semaphore);
        s_ack_semaphore = NULL;
    }

    esp_err_t err = esp_now_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW deinit failed: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "ESP-NOW deinitialized");
    return ESP_OK;
}

esp_err_t espnow_init_wifi(uint8_t channel, int8_t tx_power_dbm)
{
    esp_err_t err;
    
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Netif init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop create failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set mode failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s",esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set channel failed: %s", esp_err_to_name(err));
        return err;
    }
    s_current_channel = channel;

    if (tx_power_dbm > 0) {
        err = esp_wifi_set_max_tx_power(tx_power_dbm * 4); // dBm to 0.25 dBm units
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "WiFi TX power set to %d dBm", tx_power_dbm);
        } else {
            ESP_LOGW(TAG, "Failed to set TX power: %s", esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "WiFi initialized for ESP-NOW on channel %d", channel);
    return ESP_OK;
}

esp_err_t espnow_add_peer(const uint8_t *peer_mac, uint8_t channel, bool encrypt)
{
    if (!peer_mac) {
        return ESP_ERR_INVALID_ARG;
    }

    if (esp_now_is_peer_exist(peer_mac)) {
        ESP_LOGD(TAG, "Peer " MACSTR " already exists", MAC2STR(peer_mac));
        return ESP_OK;
    }

    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, peer_mac, 6);
    peer_info.channel = channel;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = encrypt;

    esp_err_t err = esp_now_add_peer(&peer_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Add peer " MACSTR " failed: %s", MAC2STR(peer_mac), esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Peer " MACSTR " added (ch=%d)", MAC2STR(peer_mac), channel);
    return ESP_OK;
}

esp_err_t espnow_remove_peer(const uint8_t *peer_mac)
{
    if (!peer_mac) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_now_del_peer(peer_mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Remove peer failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Peer " MACSTR " removed", MAC2STR(peer_mac));
    return ESP_OK;
}

bool espnow_peer_exists(const uint8_t *peer_mac)
{
    if (!peer_mac) {
        return false;
    }
    return esp_now_is_peer_exist(peer_mac);
}

esp_err_t espnow_send(const uint8_t *dest_mac, const uint8_t *data, size_t len)
{
    if (!data || len == 0 || len > ESPNOW_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_now_send(dest_mac, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Send failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGD(TAG, "Sent %d bytes", len);
    return ESP_OK;
}

espnow_send_status_t espnow_send_with_ack(const uint8_t *dest_mac,
                                          const uint8_t *data, size_t len,
                                          uint32_t timeout_ms)
{
    if (!dest_mac || !data || len == 0) {
        return ESPNOW_SEND_FAIL;
    }

    // Reset ACK flag
    s_ack_received = false;
    xSemaphoreTake(s_ack_semaphore, 0); // Clear any previous semaphore

    // Send data
    esp_err_t err = espnow_send(dest_mac, data, len);
    if (err != ESP_OK) {
        return ESPNOW_SEND_FAIL;
    }

    // Wait for ACK
    if (xSemaphoreTake(s_ack_semaphore, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        if (s_ack_received) {
            ESP_LOGD(TAG, "ACK confirmed");
            return ESPNOW_SEND_SUCCESS;
        }
    }

    ESP_LOGW(TAG, "No ACK received within %lu ms", timeout_ms);
    return ESPNOW_SEND_NO_ACK;
}

esp_err_t espnow_register_recv_callback(espnow_recv_cb_t cb)
{
    s_user_recv_cb = cb;
    ESP_LOGI(TAG, "User receive callback registered");
    return ESP_OK;
}

esp_err_t espnow_send_ack(const uint8_t *dest_mac)
{
    if (!dest_mac) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t ack_msg = ESPNOW_MSG_TYPE_ACK;
    esp_err_t err = espnow_send(dest_mac, &ack_msg, 1);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "ACK sent to " MACSTR, MAC2STR(dest_mac));
    }
    return err;
}

esp_err_t espnow_set_channel(uint8_t channel)
{
    if (channel < 1 || channel > 13) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set channel failed: %s", esp_err_to_name(err));
        return err;
    }

    s_current_channel = channel;
    ESP_LOGD(TAG, "Channel set to %d", channel);
    return ESP_OK;
}

uint8_t espnow_get_channel(void)
{
    return s_current_channel;
}

esp_err_t espnow_get_ack_responder_mac(uint8_t *mac_addr)
{
    if (!mac_addr) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(mac_addr, s_ack_responder_mac, 6);
    return ESP_OK;
}