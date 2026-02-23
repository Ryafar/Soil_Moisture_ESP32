/**
 * @file main.c
 * @brief Battery Monitor application
 * 
 * This application monitors battery voltage and sends data to InfluxDB/MQTT.
 */

#include "main.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "application/battery_monitor.h"
#include "drivers/csm_v2_driver/csm_v2_driver.h"
#include "drivers/wifi/wifi_manager.h"
#include "utils/esp_utils.h"
#include "utils/ntp_time.h"
#include "esp_mac.h"
#include "config/esp32-config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>

#if USE_MQTT
#include "drivers/mqtt/my_mqtt_driver.h"
#include "application/mqtt_sender.h"
#endif

#if USE_INFLUXDB
#include "drivers/influxdb/influxdb_client.h"
#include "application/influxdb_sender.h"
#endif

static const char *TAG = "MAIN";

#define MEASUREMENT_TASK_STACK_SIZE 8192
#define MEASUREMENT_TASK_PRIORITY 5


// MARK: Measurement Task
/**
 * @brief Measurement task - handles battery monitoring, WiFi, data transmission
 */
static void measurement_task(void* pvParameters) {
    esp_err_t ret;
    
    ESP_LOGI(TAG, "=== Soil Moisture Sensor with Deep Sleep ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());

    
    // Check if this is a deep sleep wakeup
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "Wakeup caused by timer");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            ESP_LOGI(TAG, "First boot or reset (not a deep sleep wakeup)");
            break;
    }

    // Generate device ID from MAC address
    char device_id[32];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "ESP32C3_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);


    
    // ######################################################
    // MARK: Initialize
    // ######################################################
    
    // Soil sensor configuration
    csm_v2_config_t csm_config = {
        .adc_unit = SOIL_ADC_UNIT,
        .adc_channel = SOIL_ADC_CHANNEL,
        .esp_pin_power = SOIL_SENSOR_POWER_PIN,
        .dry_voltage = SOIL_DRY_VOLTAGE_DEFAULT,
        .wet_voltage = SOIL_WET_VOLTAGE_DEFAULT,
        .enable_calibration = false
    };
    csm_v2_init(&csm_config);


    // Initialize battery monitoring
    ESP_LOGI(TAG, "Initializing battery monitoring...");
    battery_monitor_init();


#if USE_MQTT || USE_INFLUXDB
    // Initialize WiFi 
    ESP_LOGI(TAG, "Initializing WiFi...");
    wifi_manager_config_t wifi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
        .max_retry = WIFI_MAX_RETRY,
    };
    wifi_manager_init(&wifi_config, NULL);


#if USE_MQTT
    // Initialize MQTT client
    mqtt_client_config_t mqtt_config = {
        .broker_uri = MQTT_BROKER_URI,
        .username = MQTT_USERNAME,
        .password = MQTT_PASSWORD,
        .client_id = {0},
        .base_topic = MQTT_BASE_TOPIC,
        .keepalive = 60,
        .timeout_ms = 5000,
        .use_ssl = MQTT_USE_SSL,
    };
    mqtt_client_init(&mqtt_config);
#endif // USE_MQTT

#if USE_INFLUXDB
    // Initialize InfluxDB client
    influxdb_client_config_t influxdb_config = {
        .server = INFLUXDB_SERVER,
        .port = INFLUXDB_PORT,
        .bucket = INFLUXDB_BUCKET,
        .org = INFLUXDB_ORG,
        .token = INFLUXDB_TOKEN,
        .endpoint = INFLUXDB_ENDPOINT,
        .timeout_ms = 10000,
        .max_retries = 3
    };
    influxdb_client_init(&influxdb_config);
#endif // USE_INFLUXDB

#endif // USE_MQTT || USE_INFLUXDB



    // ######################################################
    // MARK: Main Measurement Loop
    // ######################################################
    
    ESP_LOGI(TAG, "Initialization complete. Starting measurements in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000)); // Short delay to ensure everything is initialized before starting measurements

    ESP_LOGI(TAG, "Starting main measurement loop...");

    battery_data_t battery_voltage_mean = {0};
    csm_v2_reading_t soil_reading_mean = {0};

    { // Measuring
        ESP_LOGI(TAG, "=== Measurement Cycle ===");

        // ======== Measure Battery ========
        battery_data_t battery_voltage_sum = {0};
        int nr_measurements = 0;
        ret = ESP_OK;

        while (nr_measurements < BATTERY_ADC_MEASUREMENTS) {
            ret |= battery_monitor_measure(&battery_voltage_mean);
            battery_voltage_sum.voltage += battery_voltage_mean.voltage;
            battery_voltage_sum.percentage += battery_voltage_mean.percentage;
            nr_measurements++;
        }
        if (ret == ESP_OK) {
            battery_voltage_mean.voltage = battery_voltage_sum.voltage / nr_measurements;
            battery_voltage_mean.percentage = battery_voltage_sum.percentage / nr_measurements;
            ESP_LOGI(TAG, "Battery Voltage: %.3f V | Percentage: %.1f%% (average of %d measurements)", 
                     battery_voltage_mean.voltage, battery_voltage_mean.percentage, nr_measurements);
        } else {
            ESP_LOGE(TAG, "Failed to measure battery voltage: %s", esp_err_to_name(ret));
        }


        // ======== Measure Soil ========
        csm_v2_reading_t soil_reading;
        nr_measurements = 0;
        ret = ESP_OK;
        ret |= csm_v2_enable_power();
        vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for sensor to stabilize

        while (nr_measurements < SOIL_ADC_MEASUREMENTS) {
            ret |= csm_v2_read(&soil_reading);
            soil_reading_mean.voltage += soil_reading.voltage;
            soil_reading_mean.moisture_percent += soil_reading.moisture_percent;
            soil_reading_mean.raw_adc += soil_reading.raw_adc;
            nr_measurements++;
        }
        ret |= csm_v2_disable_power();
        if (ret == ESP_OK) {
            soil_reading_mean.voltage /= nr_measurements;
            soil_reading_mean.moisture_percent /= nr_measurements;
            soil_reading_mean.raw_adc /= nr_measurements;
            ESP_LOGI(TAG, "Soil Voltage: %.3f V | Moisture: %.1f%% | Raw ADC: %d (average of %d measurements)", 
                     soil_reading_mean.voltage, soil_reading_mean.moisture_percent, soil_reading_mean.raw_adc, nr_measurements);
        } else {
            ESP_LOGE(TAG, "Failed to measure soil moisture: %s", esp_err_to_name(ret));
        }
    }

    // ######################################################
    // MARK: Wait for Data to be Sent
    // ######################################################
 

#if USE_MQTT || USE_INFLUXDB
    wifi_manager_connect();

    uint64_t timestamp_ms = 0;

#if NTP_ENABLED
    // NTP time sync (optional, can be skipped if not needed for timestamps)
    ntp_time_init(NULL);
    ntp_time_wait_for_sync(30000);  // 30 seconds timeout

    timestamp_ms = ntp_time_get_timestamp_ms();
#endif // NTP_ENABLED

#if USE_MQTT
    mqtt_client_connect();

    mqtt_battery_data_t mqtt_bdata = {
        .timestamp_ms = timestamp_ms,
        .voltage = battery_voltage_mean.voltage,
        .percentage = battery_voltage_mean.percentage,
    };
    strncpy(mqtt_bdata.device_id, device_id, sizeof(mqtt_bdata.device_id) - 1);
    mqtt_publish_battery_data(&mqtt_bdata);

    mqtt_soil_data_t mqtt_sdata = {
        .timestamp_ms = timestamp_ms,
        .voltage = soil_reading_mean.voltage,
        .moisture_percent = soil_reading_mean.moisture_percent,
        .raw_adc = soil_reading_mean.raw_adc,
    };
    strncpy(mqtt_sdata.device_id, device_id, sizeof(mqtt_sdata.device_id) - 1);
    mqtt_publish_soil_data(&mqtt_sdata);

    mqtt_sdata.voltage = soil_reading_mean.voltage;
    mqtt_sdata.moisture_percent = soil_reading_mean.moisture_percent;
    mqtt_sdata.raw_adc = soil_reading_mean.raw_adc;
    strncpy(mqtt_sdata.device_id, device_id, sizeof(mqtt_sdata.device_id) - 1);
    mqtt_publish_soil_data(&mqtt_sdata);

    mqtt_client_wait_published(5000);  // Wait up to 5 seconds for messages to be published
    mqtt_client_disconnect();
#endif // USE_MQTT

#if USE_INFLUXDB
    influxdb_battery_data_t influx_bdata = {
        .timestamp_ns = timestamp_ms * 1000000ULL, // Convert ms to ns
        .voltage = battery_voltage_mean.voltage,
        .percentage = battery_voltage_mean.percentage,
    };
    strncpy(influx_bdata.device_id, device_id, sizeof(influx_bdata.device_id) - 1);
    influxdb_write_battery_data(&influx_bdata);

    influxdb_soil_data_t influx_sdata = {
        .timestamp_ns = timestamp_ms * 1000000ULL, // Convert ms to ns
        .voltage = soil_reading_mean.voltage,
        .moisture_percent = soil_reading_mean.moisture_percent,
        .raw_adc = soil_reading_mean.raw_adc
    };
    strncpy(influx_sdata.device_id, device_id, sizeof(influx_sdata.device_id) - 1);
    influxdb_write_soil_data(&influx_sdata);
#endif // USE_INFLUXDB

    wifi_manager_disconnect();      
#endif // USE_MQTT || USE_INFLUXDB

    
    // ######################################################
    // MARK: Cleanup and Deep Sleep
    // ######################################################
    
    
    // Check if deep sleep is enabled
    if (DEEP_SLEEP_ENABLED) {
        ESP_LOGI(TAG, "Preparing for deep sleep...");
        
        // Configure timer wakeup
        uint64_t sleep_time_us = (uint64_t)DEEP_SLEEP_DURATION_SECONDS * 1000000ULL;
        esp_sleep_enable_timer_wakeup(sleep_time_us);
        
        ESP_LOGI(TAG, "Entering deep sleep for %d seconds...", DEEP_SLEEP_DURATION_SECONDS);
        ESP_LOGI(TAG, "============================================");
        
        // Small delay before entering deep sleep
        vTaskDelay(pdMS_TO_TICKS(DEEP_SLEEP_WAKEUP_DELAY_MS));
        
        // Enter deep sleep
        esp_deep_sleep_start();
    } else {
        ESP_LOGI(TAG, "Deep sleep disabled, restarting in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    // Task complete, delete itself
    vTaskDelete(NULL);
}







void app_main(void) {
    ESP_LOGI(TAG, "=== Battery Monitor Application ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    // Create measurement task
    xTaskCreate(
        measurement_task,
        "measurement",
        MEASUREMENT_TASK_STACK_SIZE,
        NULL,
        MEASUREMENT_TASK_PRIORITY,
        NULL
    );
}
