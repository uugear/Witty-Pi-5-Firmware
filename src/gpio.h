#ifndef _GPIO_H_
#define _GPIO_H_

#include <stdbool.h>



typedef void (*gpio_event_callback_t)(void);


/**
 * Initialize GPIO manager
 */                                                      
void gpio_manager_init(void);


/**
 * Register a callback function for a GPIO pin and specific events
 * 
 * @param gpio The GPIO pin number
 * @param event_mask The events to trigger the callback (bit mask)
 * @param callback The function to call when the event occurs
 * @return true if registration was successful, false otherwise
 */
bool gpio_register_callback(uint8_t gpio, uint32_t event_mask, gpio_event_callback_t callback);


#endif