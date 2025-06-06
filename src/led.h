#ifndef _LED_H_
#define _LED_H_

#include <stdbool.h>


#define GPIO_LED    	22


/**
 * Initialize the LED controller
 */                                                      
void led_init(void);


/**
 * Control the on-board white LED
 * 
 * @param on Whether to turn on the LED
 * @param duration Duration (in ms) before toggling the LED state, 0 means no toggle
 */
void control_led(bool on, int duration);


#endif