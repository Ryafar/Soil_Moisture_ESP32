/**
 * @file soil_monitor_app.c
 * @brief Soil Moisture Monitoring Application - Implementation
 */

#include "soil_monitor_app.h"
#include "../config/esp32-config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "SOIL_MONITOR_APP";

// Task handle for the monitoring task
static TaskHandle_t monitoring_task_handle = NULL;

// WiFi status callback
static void wifi_status_callback(wifi_status_t status, const char* ip_addr) {
    switch (status) {
        case WIFI_STATUS_CONNECTED:
            ESP_LOGI(TAG, "WiFi Connected! IP: %s", ip_addr);
            break;
        case WIFI_STATUS_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi Disconnected");
            break;
        case WIFI_STATUS_CONNECTING:
            ESP_LOGI(TAG, "WiFi Connecting...");
            break;
        case WIFI_STATUS_ERROR:
            ESP_LOGE(TAG, "WiFi Connection Error");
            break;
    }
}

/**
 * @brief Soil monitoring task
 */
static void soil_monitoring_task(void* pvParameters) {
    soil_monitor_app_t* app = (soil_monitor_app_t*)pvParameters;
    csm_v2_reading_t reading;
    
    ESP_LOGI(TAG, "Soil monitoring task started");
    
    while (app->is_running) {
        esp_err_t ret = csm_v2_read(&app->sensor_driver, &reading);
        if (ret == ESP_OK) {
            if (app->config.enable_logging) {
                ESP_LOGI(TAG, "Soil Moisture: %.1f%% | Voltage: %.3fV | Raw ADC: %d",
                         reading.moisture_percent, reading.voltage, reading.raw_adc);
            }
            
            // Send data via HTTP if enabled and WiFi is connected
            if (app->config.enable_http_sending && wifi_manager_is_connected()) {
                http_response_status_t http_status = http_client_send_soil_data(&reading, app->config.device_id);
                if (http_status == HTTP_RESPONSE_OK) {
                    if (app->config.enable_logging) {
                        ESP_LOGI(TAG, "Data sent successfully to server");
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to send data to server (status: %d)", http_status);
                }
            }
        } else {
            ESP_LOGE(TAG, "Failed to read sensor: %s", esp_err_to_name(ret));
        }
        
        vTaskDelay(pdMS_TO_TICKS(app->config.measurement_interval_ms));
    }
    
    ESP_LOGI(TAG, "Soil monitoring task stopped");
    monitoring_task_handle = NULL;
    vTaskDelete(NULL);
}

void soil_monitor_get_default_config(soil_monitor_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    config->adc_unit = ADC_UNIT_1;
    config->adc_channel = ADC_CHANNEL_0;
    config->measurement_interval_ms = 1000;
    config->enable_logging = true;
    config->dry_calibration_voltage = 3.0f;
    config->wet_calibration_voltage = 1.0f;
    config->enable_wifi = true;
    config->enable_http_sending = true;
    
    // Generate device ID from MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(config->device_id, sizeof(config->device_id), "SOIL_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t soil_monitor_init(soil_monitor_app_t* app, const soil_monitor_config_t* config) {
    if (app == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&app->config, config, sizeof(soil_monitor_config_t));
    
    // Initialize sensor driver
    csm_v2_config_t sensor_config;
    csm_v2_get_default_config(&sensor_config, config->adc_unit, config->adc_channel);
    sensor_config.dry_voltage = config->dry_calibration_voltage;
    sensor_config.wet_voltage = config->wet_calibration_voltage;
    sensor_config.enable_calibration = true;
    
    esp_err_t ret = csm_v2_init(&app->sensor_driver, &sensor_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sensor driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize WiFi if enabled
    if (config->enable_wifi) {
        wifi_manager_config_t wifi_config = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .max_retry = WIFI_MAX_RETRY
        };
        
        ret = wifi_manager_init(&wifi_config, wifi_status_callback);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize WiFi manager: %s", esp_err_to_name(ret));
            csm_v2_deinit(&app->sensor_driver);
            return ret;
        }
        
        // Initialize HTTP client if HTTP sending is enabled
        if (config->enable_http_sending) {
            http_client_config_t http_config = {
                .server_ip = HTTP_SERVER_IP,
                .server_port = HTTP_SERVER_PORT,
                .endpoint = HTTP_ENDPOINT,
                .timeout_ms = HTTP_TIMEOUT_MS,
                .max_retries = HTTP_MAX_RETRIES
            };
            
            ret = http_client_init(&http_config);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to initialize HTTP client: %s", esp_err_to_name(ret));
                wifi_manager_deinit();
                csm_v2_deinit(&app->sensor_driver);
                return ret;
            }
        }
    }
    
    app->is_running = false;
    ESP_LOGI(TAG, "Soil monitoring application initialized");
    ESP_LOGI(TAG, "Device ID: %s", app->config.device_id);
    if (config->enable_wifi) {
        ESP_LOGI(TAG, "WiFi and HTTP communication enabled");
    }
    return ESP_OK;
}

esp_err_t soil_monitor_start(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (app->is_running) {
        ESP_LOGW(TAG, "Application already running");
        return ESP_OK;
    }
    
    // Start WiFi connection if enabled
    if (app->config.enable_wifi) {
        esp_err_t ret = wifi_manager_connect();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    app->is_running = true;
    
    // Create monitoring task
    BaseType_t task_created = xTaskCreate(
        soil_monitoring_task,
        "soil_monitor",
        4096,
        app,
        5,
        &monitoring_task_handle
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitoring task");
        app->is_running = false;
        if (app->config.enable_wifi) {
            wifi_manager_disconnect();
        }
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Soil monitoring application started");
    return ESP_OK;
}

esp_err_t soil_monitor_stop(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!app->is_running) {
        ESP_LOGW(TAG, "Application not running");
        return ESP_OK;
    }
    
    app->is_running = false;
    
    // Wait for task to finish
    while (monitoring_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "Soil monitoring application stopped");
    return ESP_OK;
}

esp_err_t soil_monitor_deinit(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Stop application if running
    esp_err_t ret = soil_monitor_stop(app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop application: %s", esp_err_to_name(ret));
    }
    
    // Deinitialize HTTP client if enabled
    if (app->config.enable_http_sending) {
        http_client_deinit();
    }
    
    // Deinitialize WiFi if enabled
    if (app->config.enable_wifi) {
        wifi_manager_deinit();
    }
    
    // Deinitialize sensor driver
    ret = csm_v2_deinit(&app->sensor_driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize sensor driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Soil monitoring application deinitialized");
    return ESP_OK;
}

esp_err_t soil_monitor_calibrate(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Starting calibration sequence...");
    ESP_LOGI(TAG, "Place sensor in dry soil and wait for readings to stabilize");
    
    // Take dry reading
    vTaskDelay(pdMS_TO_TICKS(3000));  // Wait 3 seconds
    float dry_voltage;
    esp_err_t ret = csm_v2_read_voltage(&app->sensor_driver, &dry_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read dry voltage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Dry voltage recorded: %.3fV", dry_voltage);
    ESP_LOGI(TAG, "Now place sensor in wet soil and wait for readings to stabilize");
    
    // Take wet reading
    vTaskDelay(pdMS_TO_TICKS(5000));  // Wait 5 seconds
    float wet_voltage;
    ret = csm_v2_read_voltage(&app->sensor_driver, &wet_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read wet voltage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Wet voltage recorded: %.3fV", wet_voltage);
    
    // Apply calibration
    ret = csm_v2_calibrate(&app->sensor_driver, dry_voltage, wet_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply calibration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Update app config
    app->config.dry_calibration_voltage = dry_voltage;
    app->config.wet_calibration_voltage = wet_voltage;
    
    ESP_LOGI(TAG, "Calibration completed successfully!");
    return ESP_OK;
}