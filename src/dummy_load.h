#ifndef _DUMMY_LOAD_H_
#define _DUMMY_LOAD_H_

#include <stdbool.h>


#define GPIO_DUMMY_LOAD	    14


/**
 * Initialize the dummy load controller
 */                                                      
void dummy_load_init(void);


/**
 * Control the dummy load
 *
 * @param on Set true/false to turn on/off dummy load
 * @param duration Duration (in ms) before toggling the dummy load state, 0 means no toggle
 */   
void dummy_load_control(bool on, int duration);


#endif