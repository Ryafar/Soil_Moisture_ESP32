/**
 * @file wifi_manager.c
 * @brief WiFi Connection Manager Implementation
 */

#include <string.h>
#include "wifi_manager.h"
#include "../config/esp32-config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"

static const char *TAG = "WiFiManager";

// Static variables
static wifi_manager_config_t s_wifi_config;
static wifi_status_callback_t s_status_callback = NULL;
static wifi_status_t s_current_status = WIFI_STATUS_DISCONNECTED;
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static esp_netif_t *s_sta_netif = NULL;

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void update_status(wifi_status_t new_status, const char* ip_addr);

esp_err_t wifi_manager_init(const wifi_manager_config_t* config, wifi_status_callback_t callback)
{
    esp_err_t ret = ESP_OK;

    // Copy configuration
    strcpy(s_wifi_config.ssid, config->ssid);
    strcpy(s_wifi_config.password, config->password);
    s_wifi_config.max_retry = config->max_retry;
    s_status_callback = callback;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create default WiFi STA interface
    s_sta_netif = esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Set country code to help with 2.4GHz channel selection
    wifi_country_t country = {
        .cc = "CH",  // Switzerland
        .schan = 1,
        .nchan = 13,
        .max_tx_power = 20,
        .policy = WIFI_COUNTRY_POLICY_AUTO
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                       ESP_EVENT_ANY_ID,
                                                       &wifi_event_handler,
                                                       NULL,
                                                       NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                       IP_EVENT_STA_GOT_IP,
                                                       &wifi_event_handler,
                                                       NULL,
                                                       NULL));

    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(void)
{
    // First, let's scan for available networks to find the 2.4GHz one
    ESP_LOGI(TAG, "Scanning for WiFi networks...");
    
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = -127,
        },
    };
    
    // Copy SSID and password
    strcpy((char*)wifi_config.sta.ssid, s_wifi_config.ssid);
    strcpy((char*)wifi_config.sta.password, s_wifi_config.password);

    // Set WiFi mode and configuration
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    
    update_status(WIFI_STATUS_CONNECTING, NULL);
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", s_wifi_config.ssid);

    return ESP_OK;
}

esp_err_t wifi_manager_disconnect(void)
{
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    update_status(WIFI_STATUS_DISCONNECTED, NULL);
    ESP_LOGI(TAG, "WiFi disconnected");
    return ESP_OK;
}

esp_err_t wifi_manager_deinit(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    
    update_status(WIFI_STATUS_DISCONNECTED, NULL);
    ESP_LOGI(TAG, "WiFi manager deinitialized");
    return ESP_OK;
}

wifi_status_t wifi_manager_get_status(void)
{
    return s_current_status;
}

bool wifi_manager_is_connected(void)
{
    return s_current_status == WIFI_STATUS_CONNECTED;
}

esp_err_t wifi_manager_get_ip(char* ip_str)
{
    if (!wifi_manager_is_connected() || ip_str == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_sta_netif, &ip_info);
    if (ret == ESP_OK) {
        sprintf(ip_str, IPSTR, IP2STR(&ip_info.ip));
    }
    
    return ret;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "Disconnected from WiFi. Reason: %d", disconnected->reason);
        
        if (s_retry_num < s_wifi_config.max_retry) {
            // Add a small delay before retry to help with dual-band issues
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP (attempt %d/%d)", s_retry_num, s_wifi_config.max_retry);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            update_status(WIFI_STATUS_ERROR, NULL);
            ESP_LOGE(TAG, "Connect to the AP failed after %d attempts", s_wifi_config.max_retry);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        char ip_str[16];
        sprintf(ip_str, IPSTR, IP2STR(&event->ip_info.ip));
        
        ESP_LOGI(TAG, "Got IP: %s", ip_str);
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        update_status(WIFI_STATUS_CONNECTED, ip_str);
    }
}

static void update_status(wifi_status_t new_status, const char* ip_addr)
{
    if (s_current_status != new_status) {
        s_current_status = new_status;
        if (s_status_callback != NULL) {
            s_status_callback(new_status, ip_addr);
        }
    }
}