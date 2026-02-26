/**
 * @file main_hub.c
 * @brief Hub Testing Application
 * 
 * This is a testing hub that listens for ESP-NOW sensor data packets
 * and acknowledges them. Useful for testing sensor nodes without having
 * a full InfluxDB/MQTT setup.
 */

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include <string.h>
#include <stdio.h>

#include "../application/espnow_sender.h"
#include "../drivers/espnow/espnow.h"
#include "../drivers/nvs/nvs.h"
#include "../config/esp32-config.h"

static const char *TAG = "HUB";

/**
 * @brief ESP-NOW receive callback
 * 
 * Handles incoming ESP-NOW packets (sensor data) and prints them.
 */
static void espnow_recv_callback(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    if (len < 1) {
        return;
    }

    uint8_t msg_type = data[0];

    // Handle data packets
    if (msg_type == ESPNOW_MSG_TYPE_DATA && len >= sizeof(espnow_sensor_data_t)) {
        const espnow_sensor_data_t *sensor_data = (const espnow_sensor_data_t *)data;

        // Print received data
        ESP_LOGI(TAG, "=== RECEIVED SENSOR DATA ===");
        ESP_LOGI(TAG, "From MAC: " MACSTR, MAC2STR(mac_addr));
        ESP_LOGI(TAG, "Device ID: %s", sensor_data->device_id);
        ESP_LOGI(TAG, "Timestamp: %llu ms", sensor_data->timestamp_ms);
        ESP_LOGI(TAG, "Soil Voltage: %.3f V", sensor_data->soil_voltage);
        ESP_LOGI(TAG, "Soil Moisture: %.1f%%", sensor_data->soil_moisture_percent);
        ESP_LOGI(TAG, "Soil Raw ADC: %d", sensor_data->soil_raw_adc);
        ESP_LOGI(TAG, "Battery Voltage: %.3f V", sensor_data->battery_voltage);
        ESP_LOGI(TAG, "Battery Percentage: %.1f%%", sensor_data->battery_percentage);
        ESP_LOGI(TAG, "===========================");

        // Add sensor as peer temporarily to send ACK
        uint8_t current_channel = espnow_get_channel();
        esp_err_t peer_err = espnow_add_peer(mac_addr, current_channel, false);
        if (peer_err != ESP_OK && peer_err != ESP_ERR_ESPNOW_EXIST) {
            ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(peer_err));
            return;
        }

        // Send ACK back to sensor
        uint8_t ack_msg[1] = {ESPNOW_MSG_TYPE_ACK};
        esp_err_t ret = esp_now_send(mac_addr, ack_msg, sizeof(ack_msg));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "ACK sent to " MACSTR, MAC2STR(mac_addr));
        } else {
            ESP_LOGE(TAG, "Failed to send ACK: %s", esp_err_to_name(ret));
        }
    }
}

/**
 * @brief Main hub task
 */
static void hub_task(void *pvParameters)
{
    ESP_LOGI(TAG, "=== ESP32 Hub (Receiver) ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());

    // Initialize NVS
    ESP_LOGI(TAG, "Initializing NVS...");
    nvs_driver_init();

    // Load previous channel from NVS and increment for next boot
    uint8_t hub_channel = 1;
    uint8_t stored_channel = 0;
    if (nvs_driver_key_exists(NVS_NAMESPACE, "hub_channel")) {
        nvs_driver_load(NVS_NAMESPACE, "hub_channel", &stored_channel, sizeof(stored_channel));
        hub_channel = stored_channel + 1;
        if (hub_channel > 13) {
            hub_channel = 1;  // Cycle back to channel 1
        }
    }
    // Save new channel for next boot
    nvs_driver_save(NVS_NAMESPACE, "hub_channel", &hub_channel, sizeof(hub_channel));
    ESP_LOGI(TAG, "Hub channel rotating: %d (old) -> %d (new)", stored_channel, hub_channel);

    // Initialize ESP-NOW
    ESP_LOGI(TAG, "Initializing ESP-NOW...");
    esp_err_t ret = espnow_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    // Set to rotated channel
    espnow_set_channel(hub_channel);
    ESP_LOGI(TAG, "Hub listening on channel %d", hub_channel);

    // Register receive callback
    ret = espnow_register_recv_callback(espnow_recv_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register receive callback: %s", esp_err_to_name(ret));
        espnow_deinit();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Hub initialized successfully!");
    ESP_LOGI(TAG, "Listening for ESP-NOW sensor data...");
    ESP_LOGI(TAG, "Press Ctrl+C to exit");

    // Keep task running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    espnow_deinit();
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Hub Application Starting ===");

    xTaskCreate(
        hub_task,
        "hub",
        8192,
        NULL,
        5,
        NULL
    );
}
