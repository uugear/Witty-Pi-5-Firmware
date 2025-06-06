#ifndef _POWER_H_
#define _POWER_H_

#include <stdbool.h>
#include <stdint.h>


// Power source priorities
#define POWER_SOURCE_PRIORITY_VUSB  0
#define POWER_SOURCE_PRIORITY_VIN   1

// Power source polling results
#define PI_NOT_POWERED              0
#define POWERED_BY_VUSB_NO_ACTION   1
#define POWERED_BY_VIN_NO_ACTION    2
#define POWER_LOW_PENDING           3
#define POWER_LOW_SHUTDOWN          4
#define POWER_RECOVER_STARTUP       5

// Power modes
#define POWER_MODE_NONE		255
#define POWER_MODE_VUSB     0
#define POWER_MODE_VIN      1


/**
 * Initialize power manager
 */
void power_init(void);


/**
 * Controll Raspberry Pi's power
 * 
 * @param on true to power Raspberry Pi, false to cut power
 * @return true if action performed successfully, false otherwise
 */
bool power_control_pi_power(bool on);


/**
 * Request Raspberry Pi to startup
 * 
 * @param reason The reason for startup (defined by ACTION_REASON_??? macro)
 * @return true if request successfully, false otherwise
 */
bool request_startup(uint8_t reason);


/**
 * Request Raspberry Pi to shutdown
 *
 * @param restart Whether restart is needed
 * @param reason The reason for shutdown (defined by ACTION_REASON_??? macro)
 * @return true if request successfully, false otherwise
 */
bool request_shutdown(bool restart, uint8_t reason);


/**
 * Get the current power mode
 *
 * @return POWER_MODE_VUSB or POWER_MODE_VIN
 */
uint8_t get_power_mode(void);


/**
 * Reset the heartbeat checking timer when heartbeat arrives.
 */
void reset_heatbeat_checking_timer(void);


/**
 * Clear system up timer when heartbeat arrives.
 */
void clear_system_up_timer(void);


/**
 * Intermittent task when Raspberry Pi is off.
 * Will blink LED and drive dummy load according to configuration.
 *
 * @return The configured time (in ms) for the task
 */
uint8_t rpi_off_intermittent_task(void);


/**
 * Poll power sources and decide the current power mode.
 * Will also request startup/shutdown according to configuration.
 *
 * @return The integer as polling result
 */
int power_source_polling(void);


/**
 * Check whether Raspberry Pi can be woke up by VIN (higher than recovery voltage)
 *
 * @return true or false
 */
bool can_vin_turn_on_rpi(void);


/**
 * Check whether Raspberry Pi can be shut down by VIN (lower than low voltage)
 * The checking is skipped if power mode is not POWER_MODE_VIN
 *
 * @return true or false
 */
bool can_vin_turn_off_rpi(void);


/**
 * Check whether Raspberry Pi's 3.3V is currently ON
 *
 * @return true or false
 */
bool is_rpi_3V3_on(void);


/**
 * Get the latest action reason
 *
 * @return The latest action reason. Higher 4 bits for startup and lower 4 bits for shutdown.
 */
uint8_t get_action_reason(void);


#endif