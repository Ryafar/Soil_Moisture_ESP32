#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdbool.h>

#include "../drivers/mqtt/my_mqtt_driver.h"


/**
 * @brief Publish soil moisture data to MQTT
 * 
 * @param data Soil moisture measurement data
 * @return mqtt_client_status_t Status of the operation
 */
mqtt_client_status_t mqtt_publish_soil_data(const mqtt_soil_data_t* data);

/**
 * @brief Publish battery data to MQTT
 * 
 * @param data Battery measurement data
 * @return mqtt_client_status_t Status of the operation
 */
mqtt_client_status_t mqtt_publish_battery_data(const mqtt_battery_data_t* data);

