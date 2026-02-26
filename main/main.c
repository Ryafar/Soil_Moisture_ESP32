/**
 * @file main.c
 * @brief Battery Monitor application
 * 
 * This application monitors battery voltage and sends data to InfluxDB/MQTT.
 */

#include "main.h"
#include "esp_log.h"
#include "esp_system.h" // For esp_reset_reason()
#include "esp_sleep.h"
#include "application/battery_monitor.h"
#include "drivers/csm_v2_driver/csm_v2_driver.h"
#include "drivers/wifi/wifi_manager.h"
#include "drivers/nvs/nvs.h"
#include "utils/esp_utils.h"
#include "utils/ntp_time.h"
#include "esp_mac.h"
#include "config/esp32-config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>

#if USE_ESPNOW
#include "application/espnow_sender.h"
#endif // USE_ESPNOW

#if USE_MQTT
#include "drivers/mqtt/my_mqtt_driver.h"
#include "application/mqtt_sender.h"
#endif // USE_MQTT

#if USE_INFLUXDB
#include "drivers/influxdb/influxdb_client.h"
#include "application/influxdb_sender.h"
#endif // USE_INFLUXDB

typedef struct {
    char device_id[32];
    uint8_t espnow_hub_mac[6];
    uint8_t wifi_current_channel;
} app_config_t;





static const char *TAG = "MAIN";
static bool is_first_boot = false;
static bool battery_is_dead = false;

#define MEASUREMENT_TASK_STACK_SIZE 8192
#define MEASUREMENT_TASK_PRIORITY   5
#define USE_WIFI                    (USE_MQTT || USE_INFLUXDB) // WiFi is needed if either MQTT or InfluxDB is used


// Static initial application configuration (is loaded/saved from NVS)
static app_config_t app_config = {
    .device_id = {0},
    .espnow_hub_mac = ESPNOW_DEFAULT_BROADCAST_ADDRESS, // Default to broadcast (discovery mode)
    .wifi_current_channel = WIFI_DEFAULT_CHANNEL
};




// MARK: Measurement Task
/**
 * @brief Measurement task - handles battery monitoring, WiFi, data transmission
 */
static void measurement_task(void* pvParameters) {
    esp_err_t ret;
    
    ESP_LOGI(TAG, "=== Soil Moisture Sensor with Deep Sleep ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());



    
    // #######################################################
    // MARK: Wakeup
    // #######################################################

    // Check if this is a deep sleep wakeup
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
    esp_reset_reason_t reset_reason = esp_reset_reason();

    if (wake_cause == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI("BOOT", "Woke up from deep sleep timer");
    } else {
        if (reset_reason == ESP_RST_EXT) {
            ESP_LOGI("BOOT", "Reset button pressed"); // state not detected on lolin lite
        } 
        else if (reset_reason == ESP_RST_POWERON) {
            ESP_LOGI("BOOT", "Power-on reset (likely first boot after flash)");
        } 
        else {
            ESP_LOGI("BOOT", "Other reset reason: %d", reset_reason);
        }

        // Not from Deep Sleep -> send initialization message to MQTT for homeassistant
        is_first_boot = true;
    }

    printf(is_first_boot ? "First boot detected" : "Wakeup from deep sleep detected");

    if (is_first_boot) {
        ESP_LOGI(TAG, "Performing first boot initialization...");

        // Generate device ID from MAC address
        char device_id[32] = {0};
        generate_device_id_from_wifi_mac(device_id, sizeof(device_id), DEVICE_ID_PREFIX);
        strncpy(app_config.device_id, device_id, sizeof(app_config.device_id) - 1);
        ESP_LOGI(TAG, "Generated Device ID: %s", app_config.device_id);

    } else {
        ESP_LOGI(TAG, "Performing wakeup initialization...");
    }
    


    
    // ######################################################
    // MARK: Initialize
    // ######################################################
    
    // Initialize NVS and load config (or save initial config on first boot)
    ESP_LOGI(TAG, "Initializing NVS...");
    nvs_driver_init();
    if (is_first_boot) {
        // Save initial config to NVS on first boot
        nvs_driver_save(NVS_NAMESPACE, NVS_KEY_APP_CONFIG, &app_config, sizeof(app_config));
    } else {
        // Load config from NVS on wakeup
        nvs_driver_load(NVS_NAMESPACE, NVS_KEY_APP_CONFIG, &app_config, sizeof(app_config));
        ESP_LOGI(TAG, "Loaded config from NVS:");
        ESP_LOGI(TAG, "    Device ID: %s", app_config.device_id);
        ESP_LOGI(TAG, "    Hub MAC: " MACSTR, MAC2STR(app_config.espnow_hub_mac));
        ESP_LOGI(TAG, "    Current WiFi Channel: %d", app_config.wifi_current_channel);
    }

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


    // Initialize WiFi 
#if USE_WIFI
    ESP_LOGI(TAG, "Initializing WiFi...");
    wifi_manager_config_t wifi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
        .max_retry = WIFI_MAX_RETRY,
    };
    wifi_manager_init(&wifi_config, NULL);
#endif // USE_WIFI

    // Initialize ESP-NOW
#if USE_ESPNOW
    espnow_sender_config_t espnow_config = {
        .hub_mac = {0},
        .start_channel = app_config.wifi_current_channel,
        .max_retries = 3,
        .retry_delay_ms = 200,
        .ack_timeout_ms = 500
    };
    strncpy((char*)espnow_config.hub_mac, (char*)app_config.espnow_hub_mac, 6);

    if (USE_WIFI) {
        espnow_sender_init_on_existing_wifi(&espnow_config, app_config.wifi_current_channel);
    } else {
        // If WiFi is not used, initialize ESP-NOW sender which also initializes WiFi in STA mode
        espnow_sender_init(&espnow_config, app_config.wifi_current_channel, 0);
    }
#endif // USE_ESPNOW


    // Initialize MQTT client
#if USE_MQTT
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

    // Check battery level
    if (battery_voltage_mean.voltage <= BATTERY_MONITOR_LOW_VOLTAGE_THRESHOLD) {
        ESP_LOGW(TAG, "Battery voltage (%.3f V) is below minimum threshold (%.3f V).", battery_voltage_mean.voltage, BATTERY_MONITOR_LOW_VOLTAGE_THRESHOLD);
        
        battery_is_dead = true;
    }

    // ######################################################
    // MARK: Wait for Data to be Sent
    // ######################################################
 
    if (battery_is_dead) {
        ESP_LOGW(TAG, "Battery is too low. Skipping data transmission and entering deep sleep to save power.");
    } else {


#if USE_WIFI
        wifi_manager_connect();

    #if NTP_ENABLED
        // NTP time sync (optional, can be skipped if not needed for timestamps)
        ntp_time_init(NULL);
        ntp_time_wait_for_sync(30000);  // 30 seconds timeout
    #endif // NTP_ENABLED
#endif // USE_WIFI

        // Get current timestamp, if NTP not synced, returns 0
        uint64_t timestamp_ms = ntp_time_get_timestamp_ms();

        // Send data via ESP-NOW
#if USE_ESPNOW
        espnow_sensor_data_t espnow_data;
        espnow_sender_build_packet(&espnow_data,
            app_config.device_id,
            timestamp_ms,
            soil_reading_mean.voltage,
            soil_reading_mean.moisture_percent,
            soil_reading_mean.raw_adc,
            battery_voltage_mean.voltage,
            battery_voltage_mean.percentage);

        // Check if in discovery mode (hub MAC is broadcast address)
        bool is_discovery_mode = espnow_sender_is_broadcast_mac(app_config.espnow_hub_mac);

        uint8_t ack_responder_mac[6] = {0};
        uint8_t previous_channel = app_config.wifi_current_channel;
        
        espnow_sender_status_t send_status = espnow_sender_send_data(&espnow_data, 
                                                                     &app_config.wifi_current_channel,
                                                                     ack_responder_mac);
        
        if (send_status == ESPNOW_SENDER_OK) {
            ESP_LOGI(TAG, "Data sent successfully via ESP-NOW on channel %d", 
                    app_config.wifi_current_channel);
            
            // In discovery mode, save the discovered hub MAC (if valid)
            if (is_discovery_mode && espnow_sender_is_mac_valid(ack_responder_mac)) {
                memcpy(app_config.espnow_hub_mac, ack_responder_mac, 6);
                ESP_LOGI(TAG, "Hub discovered: " MACSTR, MAC2STR(ack_responder_mac));
            }
            
            // Save config if channel changed OR hub was discovered
            if (previous_channel != app_config.wifi_current_channel || is_discovery_mode) {
                nvs_driver_save(NVS_NAMESPACE, NVS_KEY_APP_CONFIG, &app_config, sizeof(app_config));
                ESP_LOGI(TAG, "Config saved to NVS (channel=%d, hub=" MACSTR ")", 
                        app_config.wifi_current_channel, MAC2STR(app_config.espnow_hub_mac));
            }
        } else {
            ESP_LOGE(TAG, "Failed to send data via ESP-NOW: %d", send_status);
        }
#endif // USE_ESPNOW

#if USE_MQTT
        mqtt_client_connect();

        if (is_first_boot) {
            mqtt_publish_soil_sensor_homeassistant_discovery(app_config.device_id);
        }

        mqtt_battery_data_t mqtt_bdata = {
            .timestamp_ms = timestamp_ms,
            .voltage = battery_voltage_mean.voltage,
            .percentage = battery_voltage_mean.percentage,
        };
        strncpy(mqtt_bdata.device_id, app_config.device_id, sizeof(mqtt_bdata.device_id) - 1);
        mqtt_publish_battery_data(&mqtt_bdata);

        mqtt_soil_data_t mqtt_sdata = {
            .timestamp_ms = timestamp_ms,
            .voltage = soil_reading_mean.voltage,
            .moisture_percent = soil_reading_mean.moisture_percent,
            .raw_adc = soil_reading_mean.raw_adc,
        };
        strncpy(mqtt_sdata.device_id, app_config.device_id, sizeof(mqtt_sdata.device_id) - 1);
        mqtt_publish_soil_data(&mqtt_sdata);

        mqtt_sdata.voltage = soil_reading_mean.voltage;
        mqtt_sdata.moisture_percent = soil_reading_mean.moisture_percent;
        mqtt_sdata.raw_adc = soil_reading_mean.raw_adc;
        strncpy(mqtt_sdata.device_id, app_config.device_id, sizeof(mqtt_sdata.device_id) - 1);
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
        strncpy(influx_bdata.device_id, app_config.device_id, sizeof(influx_bdata.device_id) - 1);
        influxdb_write_battery_data(&influx_bdata);

        influxdb_soil_data_t influx_sdata = {
            .timestamp_ns = timestamp_ms * 1000000ULL, // Convert ms to ns
            .voltage = soil_reading_mean.voltage,
            .moisture_percent = soil_reading_mean.moisture_percent,
            .raw_adc = soil_reading_mean.raw_adc
        };
        strncpy(influx_sdata.device_id, app_config.device_id, sizeof(influx_sdata.device_id) - 1);
        influxdb_write_soil_data(&influx_sdata);
#endif // USE_INFLUXDB

#if USE_WIFI
        wifi_manager_disconnect();      
#endif // USE_WIFI


    } // end if batter_is_dead
    



    // ######################################################
    // MARK: Cleanup and Deep Sleep
    // ######################################################
    


    // Check if deep sleep is enabled
    if (DEEP_SLEEP_ENABLED || battery_is_dead) {
        ESP_LOGI(TAG, "Preparing for deep sleep...");
        
        // Configure timer wakeup
        uint64_t sleep_time_us = (uint64_t)DEEP_SLEEP_DURATION_SECONDS * 1000000ULL;
        if (battery_is_dead) {
            ESP_LOGW(TAG, "Battery is dead. Entering deep without a wakeup timer.");
        } else {
         esp_sleep_enable_timer_wakeup(sleep_time_us);
        
            ESP_LOGI(TAG, "Entering deep sleep for %d seconds...", DEEP_SLEEP_DURATION_SECONDS);
        }
        ESP_LOGI(TAG, "============================================");
        
        // Small delay before entering deep sleep
        vTaskDelay(pdMS_TO_TICKS(DEEP_SLEEP_WAKEUP_DELAY_MS));
        
        // Enter deep sleep
        esp_deep_sleep_start();
    } else {
        ESP_LOGI(TAG, "Deep sleep disabled, restarting in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(NO_DEEP_SLEEP_RESTART_DELAY_MS));
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
