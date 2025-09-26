#include "wifi_connection_main.h"
#include "esp_log.h"



static const char *TAG = "WIFI_CONN_TESTING";

void app_main(void) {
    ESP_LOGI(TAG, "Starting WiFi Connection Testing");
    
    wifi_manager_config_t wifi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
        .max_retry = WIFI_MAX_RETRY,
    };

    wifi_manager_init(&wifi_config, NULL);
    wifi_manager_connect();

    // Get IP address
    char ip_str[WIFI_IP_STRING_MAX_LEN];
    if (wifi_manager_get_ip(ip_str) == ESP_OK) {
        ESP_LOGI(TAG, "Got IP: %s", ip_str);
    }

}
