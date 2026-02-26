########################
#   MAIN Application   #
########################

idf_component_register(SRCS "main.c"
                            "drivers/adc/adc.c"
                            "drivers/adc/adc_manager.c"
                            "application/battery_monitor.c"
                            "application/influxdb_sender.c"
                            "application/mqtt_sender.c"
                            "application/espnow_sender.c"
                            "drivers/csm_v2_driver/csm_v2_driver.c"
                            "drivers/wifi/wifi_manager.c"
                            "drivers/influxdb/influxdb_client.c"
                            "drivers/mqtt/my_mqtt_driver.c"
                            "drivers/espnow/espnow.c"
                            "drivers/nvs/nvs.c"
                            "utils/esp_utils.c"
                            "utils/ntp_time.c"
                            "drivers/led/led.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_adc nvs_flash esp_event esp-tls esp_http_client json esp_timer lwip esp_wifi esp_netif mqtt)


#######################
#   TESTING           #
#######################

# To test the HUB
# idf_component_register(SRCS "01_testing/hub_main.c"
#                                "application/espnow_sender.c"
#                                "drivers/espnow/espnow.c"
#                                "drivers/nvs/nvs.c"
#                                "utils/esp_utils.c"
#                          INCLUDE_DIRS "."
#                          REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash esp_event esp_http_client esp-tls json esp_timer lwip)

# For testing wifi connection
# idf_component_register(SRCS "01_testing/wifi_connection_main.c"
#                             "drivers/wifi/wifi_manager.c"
#                             "drivers/http/http_client.c"
#                             "utils/esp_utils.c"
#                        INCLUDE_DIRS "."
#                        REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash esp_event esp_http_client json esp_timer)

# For testing soil moisture sensor
# idf_component_register(SRCS "01_testing/soil_monitor_main.c"
#                              "drivers/csm_v2_driver/csm_v2_driver.c"
#                              "utils/esp_utils.c"
#                              "drivers/adc/adc.c"
#                              "drivers/adc/adc_manager.c"
#                        INCLUDE_DIRS "."
#                        REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash esp_event esp_http_client json esp_timer)

# For testing battery monitor
# idf_component_register(SRCS "01_testing/battery_monitor_main.c"
#                              "application/battery_monitor_task.c"
#                              "utils/esp_utils.c"
#                              "drivers/adc/adc.c"
#                              "drivers/adc/adc_manager.c"
#                              "drivers/led/led.c"
#                        INCLUDE_DIRS "."
#                        REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash esp_event esp_http_client json esp_timer)

# For testing InfluxDB connection and data transmission
# idf_component_register(SRCS "01_testing/influx_db_main.c"
#                           "drivers/wifi/wifi_manager.c"
#                           "drivers/influxdb/influxdb_client.c"
#                           "utils/esp_utils.c"
#                        INCLUDE_DIRS "."
#                        REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash esp_event esp_http_client esp-tls json esp_timer lwip)