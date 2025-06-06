#include <hardware/gpio.h>
#include <hardware/timer.h>

#include "button.h"
#include "log.h"


#define BUTTON_DEBOUNCE_US          200000
#define BUTTON_LONG_PRESS_TIME_US   5000000
#define BUTTON_CLICK_DURATION_US    100000

static Button button;




int64_t long_press_callback(alarm_id_t id, void *user_data) {
    if (button.initialized) {
        if (button.long_pressed) {
            button.long_pressed();
        }
    }
    return 0;
}


void button_pressed(void) {
    uint64_t current_time = time_us_64();
    if (button.last_state == true && current_time - button.last_press_time > button.debounce_delay) {
        button.last_state = false;
        button.last_press_time = current_time;
        button.long_press_alarm = add_alarm_in_us(BUTTON_LONG_PRESS_TIME_US, long_press_callback, NULL, true);
        debug_log("Button Down\n");
        if (button.down) {
            button.down();
        }
    }
}


void button_released(void) {
    uint64_t current_time = time_us_64();
    if (button.last_state == false && current_time - button.last_release_time > button.debounce_delay) {
        cancel_alarm(button.long_press_alarm);
        button.last_state = true;
        button.last_release_time = current_time;
        debug_log("Button Up\n");
        if (button.up) {
            button.up();
        }
    }
}


bool button_init(gpio_event_callback_t down, gpio_event_callback_t up, gpio_event_callback_t long_pressed) {

    button.gpio_pin = GPIO_BUTTON;
    button.last_press_time = 0;
    button.last_release_time = 0;
    button.last_state = true;  // true means pull-up (button not pressed)
    button.debounce_delay = BUTTON_DEBOUNCE_US;
    button.long_press_alarm = -1;
    button.down = down;
    button.up = up;
    button.long_pressed = long_pressed;
    button.initialized = true;

    gpio_init(GPIO_BUTTON);
    gpio_set_dir(GPIO_BUTTON, GPIO_IN);
    gpio_pull_up(GPIO_BUTTON);
    
	gpio_register_callback(GPIO_BUTTON, GPIO_IRQ_EDGE_FALL, button_pressed);
	gpio_register_callback(GPIO_BUTTON, GPIO_IRQ_EDGE_RISE, button_released);

	return true;
}
