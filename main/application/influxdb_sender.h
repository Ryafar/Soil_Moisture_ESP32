#ifndef INFLUXDB_SENDER_H
#define INFLUXDB_SENDER_H

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "../drivers/influxdb/influxdb_client.h"

/**
 * @brief Write soil moisture data to InfluxDB (immediate, synchronous)
 */
influxdb_response_status_t influxdb_write_soil_data(const influxdb_soil_data_t* data);

/**
 * @brief Write battery data to InfluxDB (immediate, synchronous)
 */
influxdb_response_status_t influxdb_write_battery_data(const influxdb_battery_data_t* data);

#endif // INFLUXDB_SENDER_H