# 🌱 ESP32-C6 Soil Moisture Sensor Project

## 📘 Overview
This project reads soil moisture levels using a capacitive moisture sensor connected to an ESP32-C6 microcontroller. It’s ideal for smart gardening, automated irrigation, or environmental monitoring.

## 🛠️ Hardware Requirements
- ESP32-C6 (e.g., Seeed Studio XIAO ESP32C6)
- Capacitive Soil Moisture Sensor (e.g., DIYables TLC555I-based sensor)
- Jumper wires
- Breadboard (optional)
- USB-C cable

## 🔌 Wiring
| Sensor Pin | ESP32-C6 Pin |
|------------|--------------|
| VCC        | 3.3V         |
| GND        | GND          |
| AOUT       | GPIO5 (ADC)  |

> ⚠️ Note: Ensure the sensor operates at 3.3V to avoid damaging the ESP32-C6.

## 🧑‍💻 Software Setup
- ESP-IDF installed and configured
- CMake-based project structure

## 📄 Code Snippet

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_log.h"

#define TAG "MoistureSensor"

void app_main(void) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11); // GPIO5

    while (1) {
        int raw = adc1_get_raw(ADC1_CHANNEL_5);
        float moisture = (4095 - raw) / 40.95; // Convert to percentage
        ESP_LOGI(TAG, "Moisture Level: %.2f%%", moisture);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

## 📦 Build & Flash

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## 📊 Output Example

```
I (1000) MoistureSensor: Moisture Level: 42.75%
I (2000) MoistureSensor: Moisture Level: 43.10%
```

## 🧪 Calibration Tips
- Insert the sensor into dry soil and note the ADC value.
- Repeat with wet soil.
- Adjust the conversion formula accordingly.

## 📚 References
- [ESP32 Soil Moisture Tutorial](https://esp32io.com/tutorials/esp32-soil-moisture-sensor)
- [ESP32-C6 Moisture Library (Rust)](https://github.com/yotam5/soil_moisture1.2c6)

