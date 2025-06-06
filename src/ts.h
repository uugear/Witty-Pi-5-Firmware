#ifndef _TS_H_
#define _TS_H_

#include <stdbool.h>

#include "gpio.h"


#define GPIO_TS_INT	    		9

#define TEMP_ACTION_NONE		0
#define TEMP_ACTION_STARTUP		1
#define TEMP_ACTION_SHUTDOWN	2


/**
 * Initialize the temperature sensor
 *
 * @param below Callback when temperature is below T_low
 * @param over Callback when temperature is over T_high
 */                                                      
void ts_init(gpio_event_callback_t below, gpio_event_callback_t over);


/**
 * Get temperature in millidegree Celsius
 *
 * @return the temperature in millidegree Celsius
 */
int32_t ts_get_temperature_mc(void);


/**
 * Get T_low in millidegree Celsius
 *
 * @return the temperature in millidegree Celsius
 */
int32_t ts_get_t_low_mc(void);


/**
 * Set T_low in millidegree Celsius
 *
 * @param tlow The temperature in millidegree Celsius
 */
void ts_set_t_low_mc(int32_t tlow);


/**
 * Get T_high in millidegree Celsius
 *
 * @return the temperature in millidegree Celsius
 */
int32_t ts_get_t_high_mc(void);


/**
 * Set T_high in millidegree Celsius
 *
 * @param th The temperature in millidegree Celsius
 */
void ts_set_t_high_mc(int32_t th);


/**
 * Check whether Raspberry Pi can be woke up by temperature
 *
 * @return true of false
 */
//bool can_temperature_turn_on_rpi(void);


/**
 * Check whether Raspberry Pi can be shut down by temperature
 *
 * @return true of false
 */
bool can_temperature_turn_off_rpi(void);


#endif