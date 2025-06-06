#include <hardware/gpio.h>
#include <pico/time.h>

#include "led.h"


/**
 * Initialize the LED controller
 */                                                      
void led_init(void) {
	gpio_init(GPIO_LED);
    gpio_set_dir(GPIO_LED, GPIO_OUT);
}


// Callback for turnning on the LED
int64_t led_on_callback(alarm_id_t id, void *user_data) {
    gpio_put(GPIO_LED, true);
    return 0;
}


// Callback for turnning off the LED
int64_t led_off_callback(alarm_id_t id, void *user_data) {
    gpio_put(GPIO_LED, false);
    return 0;
}


/**
 * Control the on-board white LED
 * 
 * @param on Whether to turn on the LED
 * @param duration Duration (in ms) before toggling the LED state, 0 means no toggle
 */
void control_led(bool on, int duration) {
    gpio_put(GPIO_LED, on);
    if (duration > 0) {
        add_alarm_in_ms(duration, on ? led_off_callback : led_on_callback, NULL, true);
    }
}