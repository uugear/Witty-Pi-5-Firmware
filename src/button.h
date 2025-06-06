#ifndef _BUTTON_H_
#define _BUTTON_H_

#include <stdio.h>
#include <stdbool.h>
#include <pico/time.h>

#include "gpio.h"


#define GPIO_BUTTON     3


typedef struct {
    bool initialized;
    uint gpio_pin;
    uint64_t last_press_time;
    uint64_t last_release_time;
    bool last_state;
    uint debounce_delay;
    alarm_id_t long_press_alarm;
    gpio_event_callback_t down;
    gpio_event_callback_t up;
    gpio_event_callback_t long_pressed;
} Button;


bool button_init(gpio_event_callback_t down, gpio_event_callback_t up, gpio_event_callback_t long_pressed);


#endif