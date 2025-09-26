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

    // Test HTTP connection
    http_client_config_t http_config = {
        .server_ip = HTTP_SERVER_IP,
        .server_port = HTTP_SERVER_PORT,
        .endpoint = HTTP_ENDPOINT,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .max_retries = HTTP_MAX_RETRIES,
    };

    http_client_init(&http_config);
    http_client_test_connection();


    // Cleanup
    http_client_deinit();
    wifi_manager_deinit();
    ESP_LOGI(TAG, "WiFi Connection Testing Cleanup Completed");
}
