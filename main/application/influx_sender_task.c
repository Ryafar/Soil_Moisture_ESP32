#include "influx_sender.h"
#include "esp_log.h"
#include "string.h"

#define INFLUX_SENDER_STACK   8192
#define INFLUX_SENDER_PRIO    5
#define INFLUX_QUEUE_LEN      10

static const char* TAG = "INFLUX_SENDER";

typedef enum {
    INFLUX_MSG_SOIL,
    INFLUX_MSG_BATTERY
} influx_msg_type_t;

typedef struct {
    influx_msg_type_t type;
    union {
        influxdb_soil_data_t soil;
        influxdb_battery_data_t battery;
    } payload;
} influx_msg_t;

static TaskHandle_t s_task = NULL;
static QueueHandle_t sender_queue = NULL;
static bool sender_task_started = false;
static bool is_initialized = false;

static void influx_sender_task(void* pv) {
    if (sender_queue == NULL) {
        ESP_LOGE(TAG, "Sender queue not initialized. Exiting task.");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Influx sender task started");
    sender_task_started = true;

    influx_msg_t msg;
    while (1) {
        ESP_LOGI(TAG, "Waiting for messages in Influx sender queue...");
        if (xQueueReceive(sender_queue, &msg, portMAX_DELAY) == pdTRUE) {
            influxdb_response_status_t ret;
            if (msg.type == INFLUX_MSG_SOIL) {
                ret = influxdb_write_soil_data(&msg.payload.soil);
                if (ret == INFLUXDB_RESPONSE_OK) {
                    ESP_LOGI(TAG, "Sent soil data to InfluxDB");
                } else {
                    ESP_LOGE(TAG, "Failed to send soil data to InfluxDB (status: %d)", ret);
                }
            } else if (msg.type == INFLUX_MSG_BATTERY) {
                ret = influxdb_write_battery_data(&msg.payload.battery);
                if (ret == INFLUXDB_RESPONSE_OK) {
                    ESP_LOGI(TAG, "Sent battery data to InfluxDB");
                } else {
                    ESP_LOGE(TAG, "Failed to send battery data to InfluxDB (status: %d)", ret);
                }
            } else {
                ESP_LOGW(TAG, "Received unknown message type in Influx sender task");
            }
        }
        ESP_LOGI(TAG, "Influx sender task idle...");
    }
}

esp_err_t influx_sender_init(void) {
    sender_queue = xQueueCreate(INFLUX_QUEUE_LEN, sizeof(influx_msg_t));
    if (sender_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_FAIL;
    }

    BaseType_t ok = xTaskCreate(
        influx_sender_task,
        "influx_sender",
        INFLUX_SENDER_STACK,
        NULL,
        INFLUX_SENDER_PRIO,
        &s_task
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sender task");
        return ESP_FAIL;
    }

    // Wait for task to start
    while (!sender_task_started) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    is_initialized = true;
    return ESP_OK;
}

esp_err_t influx_sender_enqueue_soil(const influxdb_soil_data_t* data) {
    if (!is_initialized || !sender_queue || !data) return ESP_ERR_INVALID_STATE;
    influx_msg_t msg = { .type = INFLUX_MSG_SOIL };
    memcpy(&msg.payload.soil, data, sizeof(*data));
    esp_err_t ret = xQueueSend(sender_queue, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Enqueued soil data to Influx sender queue: %s", esp_err_to_name(ret));
    ESP_LOGI(TAG, "Current queue length: %d", uxQueueMessagesWaiting(sender_queue));
    return ret;
}

esp_err_t influx_sender_enqueue_battery(const influxdb_battery_data_t* data) {
    if (!is_initialized || !sender_queue || !data) return ESP_ERR_INVALID_STATE;
    influx_msg_t msg = { .type = INFLUX_MSG_BATTERY };
    memcpy(&msg.payload.battery, data, sizeof(*data));
    esp_err_t ret = xQueueSend(sender_queue, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Enqueued battery data to Influx sender queue: %s", esp_err_to_name(ret));
    ESP_LOGI(TAG, "Current queue length: %d", uxQueueMessagesWaiting(sender_queue));
    return ret;
}

esp_err_t influx_sender_wait_until_empty(uint32_t timeout_ms) {
    if (!sender_queue) {
        ESP_LOGW(TAG, "Sender queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t elapsed_ms = 0;
    const uint32_t check_interval_ms = 100;
    
    ESP_LOGI(TAG, "Waiting for InfluxDB sender queue to empty...");
    
    while (uxQueueMessagesWaiting(sender_queue) > 0) {
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
        elapsed_ms += check_interval_ms;
        
        if (timeout_ms > 0 && elapsed_ms >= timeout_ms) {
            ESP_LOGW(TAG, "Timeout waiting for sender queue to empty (%lu messages remaining)", 
                     uxQueueMessagesWaiting(sender_queue));
            return ESP_ERR_TIMEOUT;
        }
    }
    
    // Give sender task a bit more time to complete the last transmission
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "InfluxDB sender queue is empty, all data sent");
    return ESP_OK;
}
