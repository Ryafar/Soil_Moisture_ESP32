/**
 * @file mqtt_sender.h
 * @brief MQTT Sender - Application Layer
 *
 * Handles publishing of soil moisture and battery data to MQTT broker.
 */

#ifndef MQTT_SENDER_H
#define MQTT_SENDER_H

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdbool.h>

#include "../drivers/mqtt/my_mqtt_driver.h"

/**
 * @brief Publish soil moisture data to MQTT
 * 
 * @param data Soil moisture measurement data
 * @return mqtt_client_status_t Status of the operation
 */
mqtt_client_status_t mqtt_publish_soil_data(const mqtt_soil_data_t* data);

/**
 * @brief Publish battery data to MQTT
 * 
 * @param data Battery measurement data
 * @return mqtt_client_status_t Status of the operation
 */
mqtt_client_status_t mqtt_publish_battery_data(const mqtt_battery_data_t* data);

/**
 * @brief Publish Home Assistant MQTT discovery messages for the soil sensor
 * 
 * @param device_id Unique device identifier (e.g. derived from MAC address)
 * @return mqtt_client_status_t Status of the operation
 */
mqtt_client_status_t mqtt_publish_soil_sensor_homeassistant_discovery(const char* device_id);

#endif // MQTT_SENDER_H