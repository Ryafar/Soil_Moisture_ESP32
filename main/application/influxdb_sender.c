

#include "influxdb_sender.h"
#include <stdio.h>

static const char* TAG = "INFLUXDB_SENDER";

influxdb_response_status_t influxdb_write_battery_data(const influxdb_battery_data_t* data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "Invalid battery data: NULL pointer");
        return INFLUXDB_RESPONSE_ERROR;
    }

    // Create InfluxDB line protocol format
    // battery,device=ESP32_XXXXXX voltage=3.7,percentage=85.0 [timestamp]
    char line_protocol[512];

    if (data->timestamp_ns == 0) {
        // No timestamp provided - let InfluxDB use server time
        // TODO removed percentage check; if problems arise, consider splitting into two formats (with/without percentage)
        snprintf(line_protocol, sizeof(line_protocol),
            "battery,device=%s voltage=%.3f,percentage=%.1f",
            data->device_id,
            data->voltage,
            data->percentage
        );
    } else {
    
#if NTP_ENABLED == 0
        ESP_LOGW(TAG, "Timestamp provided, but NTP is disabled: %llu", data->timestamp_ns);
        ESP_LOGW(TAG, "InfluxDB will place the data in the past or ignore it. Consider enabling NTP for accurate timestamps.");
#endif  
        // With NTP: Include timestamp
        if (data->percentage >= 0) {
            snprintf(line_protocol, sizeof(line_protocol),
                "battery,device=%s voltage=%.3f,percentage=%.1f %llu",
                data->device_id,
                data->voltage,
                data->percentage,
                data->timestamp_ns
            );
        } else {
            // No percentage available
            snprintf(line_protocol, sizeof(line_protocol),
                "battery,device=%s voltage=%.3f %llu",
                data->device_id,
                data->voltage,
                data->timestamp_ns
            );
        }
    }

    influxdb_response_status_t ret = influxdb_send_line_protocol(line_protocol);
    if (ret != INFLUXDB_RESPONSE_OK) {
        ESP_LOGE(TAG, "Failed to send battery data to InfluxDB (status: %d)", ret);
    } else {    
        ESP_LOGI(TAG, "Sent battery data to InfluxDB successfully");
    }
    return ret;
}

influxdb_response_status_t influxdb_write_soil_data(const influxdb_soil_data_t* data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "Invalid soil data: NULL pointer");
        return INFLUXDB_RESPONSE_ERROR;
    }

    // Create InfluxDB line protocol format
    // soil_moisture,device=ESP32_XXXXXX voltage=2.5,moisture_percent=45.2,raw_adc=2048 [timestamp]
    char line_protocol[512];

    if (data->timestamp_ns == 0) {
        // No timestamp provided - let InfluxDB use server time
        snprintf(line_protocol, sizeof(line_protocol),
            "soil_moisture,device=%s voltage=%.3f,moisture_percent=%.2f,raw_adc=%d",
            data->device_id,
            data->voltage,
            data->moisture_percent,
            data->raw_adc
        );
    } else {
#if NTP_ENABLED == 0
        ESP_LOGW(TAG, "Timestamp provided, but NTP is disabled: %llu", data->timestamp_ns);
        ESP_LOGW(TAG, "InfluxDB will place the data in the past or ignore it. Consider enabling NTP for accurate timestamps.");
#endif
        snprintf(line_protocol, sizeof(line_protocol),
            "soil_moisture,device=%s voltage=%.3f,moisture_percent=%.2f,raw_adc=%d %llu",
            data->device_id,
            data->voltage,
            data->moisture_percent,
            data->raw_adc,
            data->timestamp_ns
        );
    }

    influxdb_response_status_t ret = influxdb_send_line_protocol(line_protocol);
    if (ret != INFLUXDB_RESPONSE_OK) {
        ESP_LOGE(TAG, "Failed to send soil data to InfluxDB (status: %d)", ret);
    } else {
        ESP_LOGI(TAG, "Sent soil data to InfluxDB successfully");
    }
    return ret;
}