#include <hardware/gpio.h>
#include <pico/time.h>

#include "dummy_load.h"


/**
 * Initialize the dummy load controller
 */ 
void dummy_load_init(void) {
    gpio_init(GPIO_DUMMY_LOAD);
    gpio_set_dir(GPIO_DUMMY_LOAD, GPIO_OUT);
    gpio_put(GPIO_DUMMY_LOAD, false);
}


// Callback for turnning on the dummy load
int64_t dummy_load_on_callback(alarm_id_t id, void *user_data) {
    gpio_put(GPIO_DUMMY_LOAD, true);
    return 0;
}


// Callback for turnning off the dummy load
int64_t dummy_load_off_callback(alarm_id_t id, void *user_data) {
    gpio_put(GPIO_DUMMY_LOAD, false);
    return 0;
}


/**
 * Control the dummy load
 *
 * @param on Set true/false to turn on/off dummy load
 * @param duration Duration (in ms) before toggling the dummy load state, 0 means no toggle
 */   
void dummy_load_control(bool on, int duration) {
    gpio_put(GPIO_DUMMY_LOAD, on);
	if (duration > 0) {
        add_alarm_in_ms(duration, on ? dummy_load_off_callback : dummy_load_on_callback, NULL, true);
    }
}