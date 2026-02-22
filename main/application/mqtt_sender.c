

#include "mqtt_sender.h"
#include "../drivers/mqtt/my_mqtt_driver.h"
#include "esp_log.h"
#include "esp_event.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief Create JSON payload for soil data
 */
static char* create_soil_json_payload(const mqtt_soil_data_t* data) {
    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "timestamp", (double)data->timestamp_ms);
    cJSON_AddStringToObject(root, "device_id", data->device_id);
    cJSON_AddNumberToObject(root, "voltage", data->voltage);
    cJSON_AddNumberToObject(root, "moisture_percent", data->moisture_percent);
    cJSON_AddNumberToObject(root, "raw_adc", data->raw_adc);
    char* payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

/**
 * @brief Create JSON payload for battery data
 */
static char* create_battery_json_payload(const mqtt_battery_data_t* data) {
    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "timestamp", (double)data->timestamp_ms);
    cJSON_AddStringToObject(root, "device_id", data->device_id);
    cJSON_AddNumberToObject(root, "voltage", data->voltage);
    cJSON_AddNumberToObject(root, "percentage", data->percentage);
    char* payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

static const char* SENDER_TAG = "MQTT_SENDER";

mqtt_client_status_t mqtt_publish_soil_data(const mqtt_soil_data_t* data) {
    if (data == NULL) {
        ESP_LOGE(SENDER_TAG, "Invalid soil data");
        return MQTT_CLIENT_STATUS_INVALID_PARAM;
    }
    char* payload = create_soil_json_payload(data);
    if (payload == NULL) {
        ESP_LOGE(SENDER_TAG, "Failed to create JSON payload");
        return MQTT_CLIENT_STATUS_ERROR;
    }
    char topic[128];
    snprintf(topic, sizeof(topic), "soil_sensor/%s/soil", data->device_id);
    esp_err_t err = mqtt_client_publish(topic, payload, strlen(payload), 1);
    free(payload);
    if (err != ESP_OK) {
        ESP_LOGE(SENDER_TAG, "Failed to publish soil data");
        return MQTT_CLIENT_STATUS_ERROR;
    }
    ESP_LOGI(SENDER_TAG, "Soil data published to topic: %s", topic);
    return MQTT_CLIENT_STATUS_OK;
}

mqtt_client_status_t mqtt_publish_battery_data(const mqtt_battery_data_t* data) {
    if (data == NULL) {
        ESP_LOGE(SENDER_TAG, "Invalid battery data");
        return MQTT_CLIENT_STATUS_INVALID_PARAM;
    }
    char* payload = create_battery_json_payload(data);
    if (payload == NULL) {
        ESP_LOGE(SENDER_TAG, "Failed to create JSON payload");
        return MQTT_CLIENT_STATUS_ERROR;
    }
    char topic[128];
    snprintf(topic, sizeof(topic), "soil_sensor/%s/battery", data->device_id);
    esp_err_t err = mqtt_client_publish(topic, payload, strlen(payload), 1);
    free(payload);
    if (err != ESP_OK) {
        ESP_LOGE(SENDER_TAG, "Failed to publish battery data");
        return MQTT_CLIENT_STATUS_ERROR;
    }
    ESP_LOGI(SENDER_TAG, "Battery data published to topic: %s", topic);
    return MQTT_CLIENT_STATUS_OK;
}