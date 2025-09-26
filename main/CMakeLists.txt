########################
#   MAIN Application   #
########################

# idf_component_register(SRCS "soil_sensor_example_main.c"
#                             "hal/adc_hal.c"
#                             "csm_v2_driver/csm_v2_driver.c"
#                             "application/soil_monitor_app.c"
#                             "wifi/wifi_manager.c"
#                             "http/http_client.c"
#                        INCLUDE_DIRS "."
#                        REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash esp_event esp_http_client json)


# For testing wifi connection
idf_component_register(SRCS "01_testing/wifi_connection_main.c"
                            "wifi/wifi_manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash esp_event esp_http_client json)
