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
    esp_err_t err = mqtt_client_publish(topic, payload, strlen(payload), 1, 1);
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
    esp_err_t err = mqtt_client_publish(topic, payload, strlen(payload), 1, 1);
    free(payload);
    if (err != ESP_OK) {
        ESP_LOGE(SENDER_TAG, "Failed to publish battery data");
        return MQTT_CLIENT_STATUS_ERROR;
    }
    ESP_LOGI(SENDER_TAG, "Battery data published to topic: %s", topic);
    return MQTT_CLIENT_STATUS_OK;
}


/**
 * @brief Publish a single Home Assistant MQTT discovery message
 */
static mqtt_client_status_t publish_ha_discovery(
    const char* device_id,
    const char* entity_id,
    const char* name,
    const char* state_topic,
    const char* value_template,
    const char* unit,
    const char* precision,
    const char* device_class,
    const char* state_class)
{
    char topic[160];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_%s/config", device_id, entity_id);

    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(SENDER_TAG, "Failed to create JSON object for %s", entity_id);
        return MQTT_CLIENT_STATUS_ERROR;
    }

    // char full_name[64];
    // snprintf(full_name, sizeof(full_name), "%s %s", device_id, name);

    // char unique_id[64];
    // snprintf(unique_id, sizeof(unique_id), "%s_%s", device_id, entity_id);

    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "unique_id", entity_id); // entity_id in homeassistant is (device_name + unique_id)
    cJSON_AddStringToObject(root, "state_topic", state_topic);
    cJSON_AddStringToObject(root, "value_template", value_template);
    cJSON_AddStringToObject(root, "unit_of_measurement", unit);
    cJSON_AddStringToObject(root, "suggested_display_precision", precision);
    if (device_class != NULL) {
        cJSON_AddStringToObject(root, "device_class", device_class);
    }
    if (state_class != NULL) {
        cJSON_AddStringToObject(root, "state_class", state_class);
    }

    /* Device grouping object */
    cJSON* device = cJSON_CreateObject();
    cJSON* identifiers = cJSON_CreateArray();
    cJSON_AddItemToArray(identifiers, cJSON_CreateString(device_id));
    cJSON_AddItemToObject(device, "identifiers", identifiers);
    char device_name[64];
    snprintf(device_name, sizeof(device_name), "Soil Sensor %s", device_id);
    cJSON_AddStringToObject(device, "name", device_name);
    cJSON_AddStringToObject(device, "model", "ESP32 Soil Moisture Sensor");
    cJSON_AddStringToObject(device, "manufacturer", "DIY");
    cJSON_AddItemToObject(root, "device", device);

    char* payload = cJSON_PrintUnformatted(root);
    // printf("Publishing HA discovery for %s: %s\n", entity_id, payload);
    cJSON_Delete(root);
    if (payload == NULL) {
        ESP_LOGE(SENDER_TAG, "Failed to create JSON payload for %s", entity_id);
        return MQTT_CLIENT_STATUS_ERROR;
    }
    esp_err_t err = mqtt_client_publish(topic, payload, strlen(payload), 1, 1);
    free(payload);
    if (err != ESP_OK) {
        ESP_LOGE(SENDER_TAG, "Failed to publish HA discovery for %s", entity_id);
        return MQTT_CLIENT_STATUS_ERROR;
    }
    ESP_LOGI(SENDER_TAG, "HA discovery published: %s", topic);
    return MQTT_CLIENT_STATUS_OK;
}

mqtt_client_status_t mqtt_publish_soil_sensor_homeassistant_discovery(const char* device_id) {
    if (device_id == NULL) {
        ESP_LOGE(SENDER_TAG, "Invalid device ID");
        return MQTT_CLIENT_STATUS_INVALID_PARAM;
    }

    char soil_topic[128];
    snprintf(soil_topic, sizeof(soil_topic), "soil_sensor/%s/soil", device_id);

    char battery_topic[128];
    snprintf(battery_topic, sizeof(battery_topic), "soil_sensor/%s/battery", device_id);

    mqtt_client_status_t status;

    status = publish_ha_discovery(device_id, "soil_voltage", "Soil Voltage",
        soil_topic, "{{ value_json.voltage }}", "V", "3", "voltage", "measurement");
    if (status != MQTT_CLIENT_STATUS_OK) return status;

    status = publish_ha_discovery(device_id, "soil_moisture", "Soil",
        soil_topic, "{{ value_json.moisture_percent }}", "%", "2", "moisture", "measurement");
    if (status != MQTT_CLIENT_STATUS_OK) return status;

    status = publish_ha_discovery(device_id, "battery_voltage", "Battery Voltage",
        battery_topic, "{{ value_json.voltage }}", "V", "3", "voltage", "measurement");
    if (status != MQTT_CLIENT_STATUS_OK) return status;

    status = publish_ha_discovery(device_id, "battery_percent", "Battery",
        battery_topic, "{{ value_json.percentage }}", "%", "2", "battery", "measurement");
    return status;
}