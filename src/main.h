#ifndef _MAIN_H_
#define _MAIN_H_

#include <stdbool.h>


#define FIRMWARE_ID 			0x51
#define FIRMWARE_VERSION_MAJOR	1
#define FIRMWARE_VERSION_MINOR	0

#define PRODUCT_NAME            "Witty Pi 5"

#define I2C_SLAVE_ADDR          0x51

#define STATE_OFF               0
#define STATE_STARTING          1
#define STATE_ON                2
#define STATE_STOPPING          3


extern uint8_t current_rpi_state;   // OFF -> STARTING -> ON -> STOPPING -> OFF


/**
 * Print the current state of Raspberry Pi in log
 */
void log_current_rpi_state(void);


/**
 * Check whether emulated USB mass storage device is mounted
 * 
 * @return true if USB-drive is mounted, false otherwise
 */
bool is_usb_msc_device_mounted(void);


/**
 * Check whether Raspberry Pi is powered
 * 
 * @return true if Raspberry Pi is powered, false otherwise
 */
bool is_rpi_powered(void);


#endif