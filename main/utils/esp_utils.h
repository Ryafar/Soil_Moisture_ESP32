#ifndef ESP_UTILS_H
#define ESP_UTILS_H

#include <stdint.h>
#include <stddef.h>


/**
 * @brief Get current timestamp in milliseconds
 * 
 * Returns the current time as milliseconds since Unix epoch (Jan 1, 1970).
 * !: Requires NTP synchronization for accurate real-world time,
 *    otherwise returns time since ESP32 boot.
 * 
 * @return uint64_t Timestamp in milliseconds
 */
uint64_t esp_utils_get_timestamp_ms(void);

/**
 * @brief Get system uptime in milliseconds
 * 
 * Returns the time elapsed since the ESP32 was powered on or reset.
 * This is independent of real-world time synchronization.
 * 
 * @return uint64_t Uptime in milliseconds
 */
uint64_t esp_utils_get_uptime_ms(void);

/**
 * @brief Generate a unique device ID string based on the ESP32's WiFi MAC address
 * 
 * The generated device ID will be in the format "PREFIX_XXXXXXXXXXXX"
 * if prefix is NULL, device ID will be in the format "ESP32_XXXXXXXXXXXX"
 * 
 * @param device_id Buffer to receive the generated device ID string
 * @param max_len Maximum length of the device_id buffer (should be at least 32 bytes)
 * @param prefix Optional prefix for the device ID (e.g. "ESP32C3"), if NULL defaults to "ESP32"
 */
void generate_device_id_from_wifi_mac(char* device_id, size_t max_len, const char* prefix);


#endif // ESP_UTILS_H