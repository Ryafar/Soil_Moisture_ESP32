#ifndef LED_H
#define LED_H

#include "driver/gpio.h"
#include "esp_err.h"

#define LED_STATE_ON  0
#define LED_STATE_OFF 1

esp_err_t led_init(gpio_num_t gpio_num);
esp_err_t led_set_state(gpio_num_t gpio_num, bool state);

#endif // LED_H