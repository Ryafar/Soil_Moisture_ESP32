#include "esp_utils.h"
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_mac.h"

static const char *TAG = "ESP_UTILS";

uint64_t esp_utils_get_timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}

uint64_t esp_utils_get_uptime_ms(void)
{
    return esp_timer_get_time() / 1000; // Convert microseconds to milliseconds
}


void generate_device_id_from_wifi_mac(char* device_id, size_t max_len, const char* prefix) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    if (prefix == NULL) {
        snprintf(device_id, max_len, "ESP32_%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        // Check if prefix ends with underscore
        size_t prefix_len = strlen(prefix);
        bool has_underscore = (prefix_len > 0 && prefix[prefix_len - 1] == '_');
        
        if (has_underscore) {
            snprintf(device_id, max_len, "%s%02X%02X%02X%02X%02X%02X",
                     prefix, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            snprintf(device_id, max_len, "%s_%02X%02X%02X%02X%02X%02X",
                     prefix, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    }
}
