########################
#   MAIN Application   #
########################

# idf_component_register(SRCS "soil_sensor_example_main.c"
#                             "drivers/hal/adc_hal.c"
#                             "drivers/csm_v2_driver/csm_v2_driver.c"
#                             "application/soil_monitor_app.c"
#                             "drivers/wifi/wifi_manager.c"
#                             "application/http/http_client.c"
#                             "utils/esp_utils.c"
#                        INCLUDE_DIRS "."
#                        REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash esp_event esp_http_client json esp_timer)


# For testing wifi connection
# idf_component_register(SRCS "01_testing/wifi_connection_main.c"
#                             "drivers/wifi/wifi_manager.c"
#                             "drivers/http/http_client.c"
#                             "utils/esp_utils.c"
#                        INCLUDE_DIRS "."
#                        REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash esp_event esp_http_client json esp_timer)

# For testing soil moisture sensor
idf_component_register(SRCS "01_testing/soil_monitor_main.c"
                             "drivers/csm_v2_driver/csm_v2_driver.c"
                             "utils/esp_utils.c"
                             "drivers/hal/adc_hal.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash esp_event esp_http_client json esp_timer)