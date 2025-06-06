#ifndef _CONF_H_
#define _CONF_H_

#include <stdint.h>
#include <stdbool.h>


#define CONF_ADDRESS            "ADDRESS"           // I2C slave address: defaul=0x51

#define CONF_DEFAULT_ON_DELAY   "DEFAULT_ON_DELAY"  // The delay (in second) between power connection and turning on Pi: default=255(off)
#define CONF_POWER_CUT_DELAY    "POWER_CUT_DELAY"   // The delay (in second) between Pi shutdown and power cut: default=15

#define CONF_PULSE_INTERVAL     "PULSE_INTERVAL"    // Pulse interval in seconds, for LED and dummy load: default=5
#define CONF_BLINK_LED          "BLINK_LED"         // How long the white LED should stay on (in ms), 0 if white LED should not blink.
#define CONF_DUMMY_LOAD         "DUMMY_LOAD"        // How long the dummy load should be applied (in ms), 0 if dummy load is off.

#define CONF_LOW_VOLTAGE        "LOW_VOLTAGE"       // Low voltage threshold (x10), 0=disabled
#define CONF_RECOVERY_VOLTAGE   "RECOVERY_VOLTAGE"  // Voltage (x10) that triggers recovery, 0=disabled

#define CONF_PS_PRIORITY        "PS_PRIORITY"       // Power source priority, 0=Vusb first, 1=Vin first

#define CONF_ADJ_VUSB           "ADJ_VUSB"          // Adjustment for measured Vusb (x100), range from -127 to 127
#define CONF_ADJ_VIN            "ADJ_VIN"           // Adjustment for measured Vin (x100), range from -127 to 127
#define CONF_ADJ_VOUT           "ADJ_VOUT"          // Adjustment for measured Vout (x100), range from -127 to 127
#define CONF_ADJ_IOUT           "ADJ_IOUT"          // Adjustment for measured Iout (x1000), range from -127 to 127

#define CONF_WATCHDOG           "WATCHDOG"          // Allowed missing heartbeats before power cycle by watchdog, default=0(disable watchdog)

#define CONF_LOG_TO_FILE		"LOG_TO_FILE"	    // Whether to write log into file: 1=allowed, 0=not allowed

#define CONF_BOOTSEL_FTY_RST	"BOOTSEL_FTY_RST"	// Whether to allow long press BOOTSEL and then click button for factory reset: 1=allowed, 0=not allowed

#define CONF_ALARM1_SECOND      "ALARM1_SECOND"     // Second_alarm register for startup alarm (BCD format)
#define CONF_ALARM1_MINUTE      "ALARM1_MINUTE"     // Minute_alarm register for startup alarm (BCD format)
#define CONF_ALARM1_HOUR        "ALARM1_HOUR"       // Hour_alarm register for startup alarm (BCD format)
#define CONF_ALARM1_DAY         "ALARM1_DAY"        // Day_alarm register for startup alarm (BCD format)

#define CONF_ALARM2_SECOND      "ALARM2_SECOND"     // Second_alarm register for shutdown alarm (BCD format)
#define CONF_ALARM2_MINUTE      "ALARM2_MINUTE"     // Minute_alarm register for shutdown alarm (BCD format)
#define CONF_ALARM2_HOUR        "ALARM2_HOUR"       // Hour_alarm register for shutdown alarm (BCD format)
#define CONF_ALARM2_DAY         "ALARM2_DAY"        // Day_alarm register for shutdown alarm (BCD format)

#define CONF_BELOW_TEMP_ACTION  "BELOW_TEMP_ACTION" // Action for below temperature: 0-do nothing; 1-shutdown; 2-startup
#define CONF_BELOW_TEMP_POINT   "BELOW_TEMP_POINT"  // Set point for below temperature (signed degrees of Celsius)
#define CONF_OVER_TEMP_ACTION   "OVER_TEMP_ACTION"  // Action for over temperature: 0-do nothing; 1-shutdown; 2-startup
#define CONF_OVER_TEMP_POINT    "OVER_TEMP_POINT"   // Set point for over temperature (signed degrees of Celsius)

#define CONF_DST_OFFSET         "DST_OFFSET"        // b7=mode; b6~b0: DST offset in minute, default=0(disable DST)
#define CONF_DST_BEGIN_MON      "DST_BEGIN_MON"     // DST begin month in BCD format
#define CONF_DST_BEGIN_DAY      "DST_BEGIN_DAY"     // mode=0: b7~b4=week in BCD, b3~b0=day in BCD; mode=1: b7~b0=date in BCD
#define CONF_DST_BEGIN_HOUR     "DST_BEGIN_HOUR"    // DST begin hour in BCD format
#define CONF_DST_BEGIN_MIN      "DST_BEGIN_MIN"     // DST begin minute in BCD format
#define CONF_DST_END_MON        "DST_END_MON"       // DST end month in BCD format
#define CONF_DST_END_DAY        "DST_END_DAY"       // mode=0: b7~b4=week in BCD, b3~b0=day in BCD; mode=1: b7~b0=date in BCD
#define CONF_DST_END_HOUR       "DST_END_HOUR"      // DST end hour in BCD format
#define CONF_DST_END_MIN        "DST_END_MIN"       // DST end minute in BCD format
#define CONF_DST_APPLIED        "DST_APPLIED"       // Whether DST has been applied

#define CONF_SYS_CLOCK_MHZ		"SYS_CLOCK_MHZ"		// System clock (in MHz) for RP2350: default=48 (required for USB drive and USB-uart)

#define CONF_MAX_KEY_LENGTH    32
#define CONF_MAX_ITEMS         64


typedef void (*item_changed_callback_t)(const char *key, uint8_t old_val, uint8_t new_val);


typedef struct {
    char key[CONF_MAX_KEY_LENGTH];
    uint8_t value;
	item_changed_callback_t callback;
} conf_item_t;


typedef struct {
    conf_item_t items[CONF_MAX_ITEMS];
    uint8_t count;
} conf_obj_t;


extern conf_obj_t config;


/**
 * Initialize configuration
 */
void conf_init(void);


/**
 * Get configuration item
 * 
 * @param key The item key
 * @return The value of configuration item
 */
uint8_t conf_get(const char *key);


/**
 * Set configuration item
 * 
 * @param key The item key
 * @param value The item value
 * @return true if succeed, false otherwise
 */
bool conf_set(const char *key, uint8_t value);


/**
 * Reset the configuration to default values
 */
void conf_reset(void);


/**
 * Synchronize the configuration in RAM with the data in file
 */
void conf_sync(void);


/**
 * Save configuration to file when:
 *   the "dirty" flag is set, and
 *   the USB drive is not mounted or ejected
 */
void process_conf_task(void);


/**
 * Check if startup alarm (ALARM1) is configured
 * 
 * @return true if configured, false otherwise
 */
bool is_startup_alarm_configured(void);


/**
 * Check if shutdown alarm (ALARM2) is configured
 * 
 * @return true if configured, false otherwise
 */
bool is_shutdown_alarm_configured(void);


/**
 * Register callback function for item changed
 * 
 * @param key The item key
 * @param callback The pointer for callback function, NULL for unregisteration
 * @return true if succeed, false otherwise
 */
bool register_item_changed_callback(const char *key, item_changed_callback_t callback);

#endif