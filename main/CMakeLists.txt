idf_component_register(SRCS "soil-sensor-example-main.c"
                            "hal/adc_hal.c"
                            "csm-v2-driver/csm-v2-driver.c"
                            "application/soil_monitor_app.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_adc)
