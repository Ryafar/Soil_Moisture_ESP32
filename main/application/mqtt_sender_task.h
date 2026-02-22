/**
 * @file mqtt_sender.h
 * @brief MQTT Sender Task - Handles asynchronous MQTT publishing
 */

#ifndef MQTT_SENDER_H
#define MQTT_SENDER_H

#include "esp_err.h"
#include "../drivers/mqtt/my_mqtt_driver.h"






// ######################################
// MARK: Task
// ######################################

/**
 * @brief Initialize and start the MQTT sender task (idempotent)
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_sender_task_init(void);

/**
 * @brief Enqueue soil data to be sent by the MQTT sender task
 * 
 * @param data Soil moisture data to send
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_sender_task_enqueue_soil(const mqtt_soil_data_t* data);

/**
 * @brief Enqueue battery data to be sent by the MQTT sender task
 * 
 * @param data Battery data to send
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_sender_task_enqueue_battery(const mqtt_battery_data_t* data);

/**
 * @brief Wait until the queue is empty (all data has been sent)
 * 
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return esp_err_t ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t mqtt_sender_task_wait_until_empty(uint32_t timeout_ms);

/**
 * @brief Deinitialize and stop the MQTT sender task
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_sender_task_deinit(void);


#endif // MQTT_SENDER_H
