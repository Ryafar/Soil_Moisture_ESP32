/**
 * @file mqtt_sender.c
 * @brief MQTT Sender Task Implementation
 * 
 * This module provides a dedicated FreeRTOS task for sending data to MQTT.
 * It uses a queue to buffer messages and sends them asynchronously.
 */

#include "mqtt_sender.h"
#include "../drivers/mqtt/my_mqtt_driver.h"
#include "../drivers/wifi/wifi_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char* TAG = "MQTT_SENDER";

#define MQTT_SENDER_TASK_STACK_SIZE 4048
#define MQTT_SENDER_TASK_PRIORITY 4
#define MQTT_SENDER_QUEUE_SIZE 20

// Message types
typedef enum {
    MQTT_MSG_TYPE_SOIL,
    MQTT_MSG_TYPE_BATTERY
} mqtt_msg_type_t;

// Queue message structure
typedef struct {
    mqtt_msg_type_t type;
    union {
        mqtt_soil_data_t soil;
        mqtt_battery_data_t battery;
    } data;
} mqtt_queue_msg_t;

// Static variables
static QueueHandle_t mqtt_queue = NULL;
static TaskHandle_t mqtt_task_handle = NULL;
static bool is_initialized = false;







// #####################################
// MARK: Sender Task
// #####################################



/**
 * @brief MQTT sender task
 */
static void mqtt_sender_task(void* pvParameters) {
    mqtt_queue_msg_t msg;
    
    ESP_LOGI(TAG, "MQTT sender task started");
    
    while (1) {
        // Wait for message in queue
        if (xQueueReceive(mqtt_queue, &msg, portMAX_DELAY) == pdTRUE) {
            
            // Check WiFi connection
            if (!wifi_manager_is_connected()) {
                ESP_LOGW(TAG, "WiFi not connected, skipping MQTT send");
                continue;
            }
            
            // Check MQTT connection
            if (!mqtt_client_is_connected()) {
                ESP_LOGW(TAG, "MQTT not connected, skipping send");
                continue;
            }
            
            // Send based on message type
            mqtt_client_status_t status;
            if (msg.type == MQTT_MSG_TYPE_SOIL) {
                status = mqtt_publish_soil_data(&msg.data.soil);
                if (status == MQTT_CLIENT_STATUS_OK) {
                    ESP_LOGI(TAG, "Soil data sent to MQTT successfully");
                } else {
                    ESP_LOGW(TAG, "Failed to send soil data to MQTT (status: %d)", status);
                }
            } else if (msg.type == MQTT_MSG_TYPE_BATTERY) {
                status = mqtt_publish_battery_data(&msg.data.battery);
                if (status == MQTT_CLIENT_STATUS_OK) {
                    ESP_LOGI(TAG, "Battery data sent to MQTT successfully");
                } else {
                    ESP_LOGW(TAG, "Failed to send battery data to MQTT (status: %d)", status);
                }
            }
            
            // Small delay to avoid overwhelming the broker
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

esp_err_t mqtt_sender_task_init(void) {
    if (is_initialized) {
        ESP_LOGD(TAG, "MQTT sender already initialized");
        return ESP_OK;
    }
    
    // Create queue
    mqtt_queue = xQueueCreate(MQTT_SENDER_QUEUE_SIZE, sizeof(mqtt_queue_msg_t));
    if (mqtt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT queue");
        return ESP_ERR_NO_MEM;
    }
    
    // Create task
    BaseType_t ret = xTaskCreate(
        mqtt_sender_task,
        "mqtt_sender",
        MQTT_SENDER_TASK_STACK_SIZE,
        NULL,
        MQTT_SENDER_TASK_PRIORITY,
        &mqtt_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT sender task");
        vQueueDelete(mqtt_queue);
        mqtt_queue = NULL;
        return ESP_FAIL;
    }
    
    is_initialized = true;
    ESP_LOGI(TAG, "MQTT sender initialized successfully");
    return ESP_OK;
}

esp_err_t mqtt_sender_task_enqueue_soil(const mqtt_soil_data_t* data) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "MQTT sender not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "Invalid soil data");
        return ESP_ERR_INVALID_ARG;
    }
    
    mqtt_queue_msg_t msg = {
        .type = MQTT_MSG_TYPE_SOIL
    };
    memcpy(&msg.data.soil, data, sizeof(mqtt_soil_data_t));
    
    if (xQueueSend(mqtt_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to enqueue soil data (queue full)");
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGD(TAG, "Soil data enqueued successfully");
    return ESP_OK;
}

esp_err_t mqtt_sender_task_enqueue_battery(const mqtt_battery_data_t* data) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "MQTT sender not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "Invalid battery data");
        return ESP_ERR_INVALID_ARG;
    }
    
    mqtt_queue_msg_t msg = {
        .type = MQTT_MSG_TYPE_BATTERY
    };
    memcpy(&msg.data.battery, data, sizeof(mqtt_battery_data_t));
    
    if (xQueueSend(mqtt_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to enqueue battery data (queue full)");
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGD(TAG, "Battery data enqueued successfully");
    return ESP_OK;
}

esp_err_t mqtt_sender_task_wait_until_empty(uint32_t timeout_ms) {
    if (!is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    while (uxQueueMessagesWaiting(mqtt_queue) > 0) {
        if ((xTaskGetTickCount() - start_tick) >= timeout_ticks) {
            ESP_LOGW(TAG, "Timeout waiting for queue to empty");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "MQTT queue is empty");
    return ESP_OK;
}

esp_err_t mqtt_sender_task_deinit(void) {
    if (!is_initialized) {
        return ESP_OK;
    }
    
    // Delete task
    if (mqtt_task_handle != NULL) {
        vTaskDelete(mqtt_task_handle);
        mqtt_task_handle = NULL;
    }
    
    // Delete queue
    if (mqtt_queue != NULL) {
        vQueueDelete(mqtt_queue);
        mqtt_queue = NULL;
    }
    
    is_initialized = false;
    ESP_LOGI(TAG, "MQTT sender deinitialized");
    return ESP_OK;
}
