#include <stdint.h>
#include <hardware/gpio.h>

#include "gpio.h"
#include "log.h"


#define MAX_GPIO_CALLBACKS 32


typedef struct {
    uint8_t gpio;
    uint32_t event_mask;
    gpio_event_callback_t callback;
} GPIO_Callback_Entry;


static GPIO_Callback_Entry gpio_callbacks[MAX_GPIO_CALLBACKS];

static uint8_t num_callbacks = 0;


/**
 * Callback function that is called by the SDK when a GPIO event occurs
 * This function checks all registered callbacks and calls the appropriate ones
 * 
 * @param gpio The GPIO pin that triggered the event
 * @param events The events that occurred (bit mask)
 */
void gpio_callback(uint gpio, uint32_t events) {
    for (uint8_t i = 0; i < num_callbacks; i++) {
        if (gpio_callbacks[i].gpio == gpio) {
            if (gpio_callbacks[i].event_mask & events) {
                if (gpio_callbacks[i].callback != NULL) {
                    gpio_callbacks[i].callback();
                }
            }
        }
    }
}


/**
 * Initialize GPIO manager
 */                                                      
void gpio_manager_init(void) {
    gpio_set_irq_callback(gpio_callback);
    irq_set_enabled(IO_IRQ_BANK0, true);
}


/**
 * Register a callback function for a GPIO pin and specific events
 * 
 * @param gpio The GPIO pin number
 * @param event_mask The events to trigger the callback (bit mask)
 * @param callback The function to call when the event occurs
 * @return true if registration was successful, false otherwise
 */
bool gpio_register_callback(uint8_t gpio, uint32_t event_mask, gpio_event_callback_t callback) {
    if (num_callbacks >= MAX_GPIO_CALLBACKS) {
        return false;
    }

    if (callback == NULL) {
        return false;
    }

    gpio_callbacks[num_callbacks].gpio = gpio;
    gpio_callbacks[num_callbacks].event_mask = event_mask;
    gpio_callbacks[num_callbacks].callback = callback;
    num_callbacks++;
    
    gpio_set_irq_enabled(gpio, event_mask, true);
    return true;
}