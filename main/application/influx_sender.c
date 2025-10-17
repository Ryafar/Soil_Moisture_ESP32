#include "influx_sender.h"
#include "esp_log.h"
#include "string.h"

#define INFLUX_SENDER_STACK   (14 * 1024)
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
static QueueHandle_t s_queue = NULL;

static void influx_sender_task(void* pv) {
    ESP_LOGI(TAG, "Influx sender task started");

    influx_msg_t msg;
    while (1) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.type == INFLUX_MSG_SOIL) {
                (void)influxdb_write_soil_data(&msg.payload.soil);
            } else if (msg.type == INFLUX_MSG_BATTERY) {
                (void)influxdb_write_battery_data(&msg.payload.battery);
            }
        }
    }
}

esp_err_t influx_sender_init(void) {
    if (s_queue == NULL) {
        s_queue = xQueueCreate(INFLUX_QUEUE_LEN, sizeof(influx_msg_t));
        if (!s_queue) {
            ESP_LOGE(TAG, "Failed to create queue");
            return ESP_FAIL;
        }
    }
    if (s_task == NULL) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            influx_sender_task,
            "influx_sender",
            INFLUX_SENDER_STACK,
            NULL,
            INFLUX_SENDER_PRIO,
            &s_task,
            0
        );
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create sender task");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t influx_sender_enqueue_soil(const influxdb_soil_data_t* data) {
    if (!s_queue || !data) return ESP_ERR_INVALID_STATE;
    influx_msg_t msg = { .type = INFLUX_MSG_SOIL };
    memcpy(&msg.payload.soil, data, sizeof(*data));
    return xQueueSend(s_queue, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t influx_sender_enqueue_battery(const influxdb_battery_data_t* data) {
    if (!s_queue || !data) return ESP_ERR_INVALID_STATE;
    influx_msg_t msg = { .type = INFLUX_MSG_BATTERY };
    memcpy(&msg.payload.battery, data, sizeof(*data));
    return xQueueSend(s_queue, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t influx_sender_wait_until_empty(uint32_t timeout_ms) {
    if (!s_queue) {
        ESP_LOGW(TAG, "Sender queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t elapsed_ms = 0;
    const uint32_t check_interval_ms = 100;
    
    ESP_LOGI(TAG, "Waiting for InfluxDB sender queue to empty...");
    
    while (uxQueueMessagesWaiting(s_queue) > 0) {
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
        elapsed_ms += check_interval_ms;
        
        if (timeout_ms > 0 && elapsed_ms >= timeout_ms) {
            ESP_LOGW(TAG, "Timeout waiting for sender queue to empty (%lu messages remaining)", 
                     uxQueueMessagesWaiting(s_queue));
            return ESP_ERR_TIMEOUT;
        }
    }
    
    // Give sender task a bit more time to complete the last transmission
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "InfluxDB sender queue is empty, all data sent");
    return ESP_OK;
}
