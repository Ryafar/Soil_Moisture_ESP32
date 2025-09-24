idf_component_register(SRCS "soil-sensor-example-main.c"
                            "hal/adc_hal.c"
                            "csm-v2-driver/csm-v2-driver.c"
                            "application/soil_monitor_app.c"
                            "wifi/wifi_manager.c"
                            "http/http_client.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_adc esp_wifi esp_netif nvs_flash esp_event esp_http_client json)
