#include <stdio.h>
#include <pico/stdlib.h>
#include <hardware/gpio.h>
#include <hardware/powman.h>
#include <hardware/clocks.h>
#include <hardware/watchdog.h>
#include <bsp/board.h>
#include <tusb.h>

#include "flash.h"
#include "log.h"
#include "rtc.h"
#include "ts.h"
#include "i2c.h"
#include "fatfs_disk.h"
#include "adc.h"
#include "conf.h"
#include "button.h"
#include "power.h"
#include "main.h"
#include "script.h"
#include "gpio.h"
#include "dummy_load.h"
#include "led.h"
#include "id_eeprom.h"
#include "util.h"
#include "bootsel_button.h"


#define VOLTAGE_CHECK_INTERVAL_US	1000000

#define ACTION_RETRY_INTERVAL_US	60000000

#define FACTORY_RESET_TIMEOUT_US	5000000

static char event_str[128];

bool output = false;

int delay = 500;

bool factory_reset_pending = false;

alarm_id_t shutdown_alarm_id = -1;
alarm_id_t startup_alarm_id = -1;
alarm_id_t postponed_action_alarm_id = -1;

alarm_id_t factory_reset_cancel_alarm_id = -1;
alarm_id_t factory_reset_blink_alarm_id = -1;

void perform_temp_action(uint8_t action, bool below, bool retry);


/**
 * Print the current state of Raspberry Pi in log
 */
void log_current_rpi_state(void) {
    switch (current_rpi_state) {
        case STATE_OFF:
            debug_log("Raspberry Pi is not powered.\n");
            break;
        case STATE_STARTING:
            debug_log("Raspberry Pi is starting up.\n");
            break;
        case STATE_ON:
            debug_log("Raspberry Pi is running.\n");
            break;
        case STATE_STOPPING:
            debug_log("Raspberry Pi is shutting down.\n");
            break;
        default:
            debug_log("Raspberry Pi is in unknown state: %d\n", current_rpi_state);
            break;
    }
}


// Callback for cancelling factory reset
int64_t factory_reset_cancel_callback(alarm_id_t id, void *user_data) {
    debug_log("Factory reset cancelled.\n");
	factory_reset_pending = false;
	cancel_alarm(factory_reset_blink_alarm_id);
    return 0;
}


// Callback for factory reset blinking
int64_t factory_reset_blink_callback(alarm_id_t id, void *user_data) {
	control_led(true, 200);
    return 600000;
}


// Callback for long pressing BOOTSEL button
void bootsel_long_pressed_callback() {
	debug_log("Factory reset is pending, wait for button click...\n");
	factory_reset_pending = true;
	cancel_alarm(factory_reset_cancel_alarm_id);
	factory_reset_cancel_alarm_id = add_alarm_in_us(FACTORY_RESET_TIMEOUT_US, factory_reset_cancel_callback, NULL, true);
	cancel_alarm(factory_reset_blink_alarm_id);
	factory_reset_blink_alarm_id = add_alarm_in_us(0, factory_reset_blink_callback, NULL, true);
}


// Callback for pressing button
void button_pressed_callback() {

}


// Callback for releasing button
void button_released_callback() {
	if (factory_reset_pending) {	// Factory reset is pending, perform it

		cancel_alarm(factory_reset_cancel_alarm_id);
		cancel_alarm(factory_reset_blink_alarm_id);
		tud_msc_start_stop_cb(0, 0, false, true);
		debug_log("Factory reset in progress...\n");
		control_led(true, 0);

		// Format the disk
		unmount_fatfs();
		flash_fatfs_init();
		mount_fatfs();
		create_default_dirs();

		// Re-initialize configration
		conf_init();

		control_led(false, 0);
		debug_log("Factory reset done.\n");
		factory_reset_pending = false;

	} else if (!is_rpi_powered()) {	// Raspberry Pi is not powered, request startup

        request_startup(ACTION_REASON_BUTTON_CLICK);

    } else {	// Raspberry Pi is powered, request shutdown

        request_shutdown(false, ACTION_REASON_BUTTON_CLICK);

    }
}


// Callback for long pressing button
void button_long_pressed_callback() {

}


// Callback for scheduled shutdown
int64_t shutdown_alarm_callback(alarm_id_t id, void *user_data) {
    debug_log("Scheduled shutdown is due.\n");
    request_shutdown(false, ACTION_REASON_ALARM2);
    return 0;
}


// Callback for scheduled startup (might also be used for retry)
int64_t startup_alarm_callback(alarm_id_t id, void *user_data) {
    debug_log("Scheduled startup is due.\n");
	bool vin_cond = !can_vin_turn_off_rpi();
	bool temp_cond = !can_temperature_turn_off_rpi();
	if (vin_cond && temp_cond) {
		request_startup(ACTION_REASON_ALARM1);
		return 0;
	} else {
		debug_log("Scheduled startup is postponed due to %s.\n", vin_cond ? (temp_cond ? "nothing" : "temperature") : (temp_cond ? "Vin" : "Vin and temperature"));
		postponed_action_alarm_id = id;
		return ACTION_RETRY_INTERVAL_US;
	}
}


// Callback when RTC alarm occurs
void rtc_alarm_occuried_callback() {
    uint8_t type = rtc_get_alarm_type();
    if (type == ALARM_TYPE_SHUTDOWN) {
		int seconds = bcd_to_dec(conf_get(CONF_ALARM2_SECOND));
        debug_log("Will shutdown in %d second.\n", seconds);
        cancel_alarm(startup_alarm_id);
        startup_alarm_id = -1;
        cancel_alarm(postponed_action_alarm_id);
        postponed_action_alarm_id = -1;
        cancel_alarm(shutdown_alarm_id);
        shutdown_alarm_id = add_alarm_in_us((int64_t)seconds * 1000000, shutdown_alarm_callback, NULL, true);
    } else if (type == ALARM_TYPE_STARTUP) {
		int seconds = bcd_to_dec(conf_get(CONF_ALARM1_SECOND));
        debug_log("Will startup in %d second.\n", seconds);
        cancel_alarm(shutdown_alarm_id);
        shutdown_alarm_id = -1;
        cancel_alarm(postponed_action_alarm_id);
        postponed_action_alarm_id = -1;
        cancel_alarm(startup_alarm_id);
        startup_alarm_id = add_alarm_in_us((int64_t)seconds * 1000000, startup_alarm_callback, NULL, true);
    } else {
        debug_log("Alarm occurs in wrong state: rpi_state=%d, alarm_type=%d\n", current_rpi_state, type);
    }
}


// Callback for retrying over-temperature startup action
int64_t retry_over_temp_startup_callback(alarm_id_t id, void *user_data) {
    perform_temp_action(TEMP_ACTION_STARTUP, false, true);
    return 0;
}


// Callback for retrying below-temperature startup action
int64_t retry_below_temp_startup_callback(alarm_id_t id, void *user_data) {
    perform_temp_action(TEMP_ACTION_STARTUP, true, true);
    return 0;
}


// Perform temperature action
void perform_temp_action(uint8_t action, bool below, bool retry) {
	if (action == TEMP_ACTION_STARTUP) {
		bool vin_cond = !can_vin_turn_off_rpi();
		bool time_cond = !can_cur_time_turn_off_rpi();
		bool state_cond = current_rpi_state == STATE_OFF;
		if (vin_cond && time_cond && state_cond) {
		    debug_log("%s-temperature startup %s.\n", below ? "Below" : "Over", retry ? "succeeds with retry" : "occurs");
			request_startup(below ? ACTION_REASON_BELOW_TEMPERATURE : ACTION_REASON_OVER_TEMPERATURE);
		} else {
			debug_log("%s-temperature startup is postponed %s (reason: %s%s%s).\n",
			    retry ? "again" : "",
				below ? "Below" : "Over",
			    vin_cond ? "" : "Vin", time_cond ? "" : "schedule", state_cond ? "" : "RPi state");
			cancel_alarm(postponed_action_alarm_id);
			postponed_action_alarm_id = add_alarm_in_us(ACTION_RETRY_INTERVAL_US,
			                                            below ? retry_below_temp_startup_callback : retry_over_temp_startup_callback,
			                                            NULL,
			                                            true);
		}
	} else if (action == TEMP_ACTION_SHUTDOWN) {
		debug_log("%s-temperature shutdown occurs.\n", below ? "Below" : "Over");
		request_shutdown(false, below ? ACTION_REASON_BELOW_TEMPERATURE : ACTION_REASON_OVER_TEMPERATURE);
	}
}


// Callback when temperature is below theshold
void ts_below_temperature_callback(void) {
	perform_temp_action(conf_get(CONF_BELOW_TEMP_ACTION), true, false);
}


// Callback when temperature is over threshold
void ts_over_temperature_callback(void) {
	perform_temp_action(conf_get(CONF_OVER_TEMP_ACTION), false, false);
}


// Callback to check Vin for possible state transition
int64_t voltage_check_callback(alarm_id_t id, void *user_data) {
    if (power_source_polling() == POWER_RECOVER_STARTUP) {
        cancel_alarm(postponed_action_alarm_id);
        postponed_action_alarm_id = -1;
    }
    return VOLTAGE_CHECK_INTERVAL_US;
}


// Firmware main function
int main() {

	stdio_init_all();

    if(!mount_fatfs()) {  // Mount file system
        bootsel_long_pressed_callback();    // No file system, prepare for factory reset
    }

    create_default_dirs();  // Create default directories

    conf_init();    // Initialize configration

	// Set System clock
	uint32_t freq_mhz = (uint32_t)conf_get(CONF_SYS_CLOCK_MHZ);
	if (freq_mhz == 48 || !set_sys_clock_khz(freq_mhz * 1000, false)) {
		set_sys_clock_48mhz();
	}

	gpio_manager_init();// Initialize GPIO manager

	id_eeprom_init();   // Initialize ID EEPROM manager

    adc_channels_init();// Initialize ADC channels

    i2c_devices_init(); // Initialize I2C devices

    rtc_init(rtc_alarm_occuried_callback); // Initialize RTC

    ts_init(ts_below_temperature_callback, ts_over_temperature_callback); // Initialize temperature sensor

    button_init(button_pressed_callback, button_released_callback, button_long_pressed_callback);  // Initialize button

    power_init();   // Initialize power manager

    dummy_load_init();  // Initialize dummy load controller

	led_init();	// Initialize LED controller

    board_init();
    tud_init(BOARD_TUD_RHPORT);

    bool auto_start = false;
	const char * reason = NULL;

    // Turn on Raspberry Pi if "Default ON" is configured
	uint8_t default_on = conf_get(CONF_DEFAULT_ON_DELAY);
	if (default_on != 255) {
		sleep_ms((uint16_t)default_on * 1000);
		debug_log("Raspberry Pi is turned on because \"Default ON\" is set.\n");
        request_startup(ACTION_REASON_POWER_CONNECTED);
	}

    // Process schedule script or schedule shutdown as per configuration
	if (!load_script(true)) {
		load_and_schedule_alarm(current_rpi_state == STATE_OFF || current_rpi_state == STATE_STOPPING);
	}

    // Keep checking voltage
    add_alarm_in_us(VOLTAGE_CHECK_INTERVAL_US, voltage_check_callback, NULL, true);

    // Main loop
    while (true) {
        tud_task();
        process_log_task();
        process_conf_task();
		if (!factory_reset_pending && conf_get(CONF_BOOTSEL_FTY_RST)) {
			check_bootsel_button(NULL, NULL, bootsel_long_pressed_callback);
		}
    }
}
