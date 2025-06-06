#include <hardware/gpio.h>
#include <pico/time.h>

#include "main.h"
#include "power.h"
#include "adc.h"
#include "i2c.h"
#include "log.h"
#include "conf.h"
#include "button.h"
#include "rtc.h"
#include "ts.h"
#include "led.h"
#include "dummy_load.h"
#include "script.h"


#define GPIO_PI_HAS_3V3     11
#define GPIO_DCDC_ENABLE    12
#define GPIO_PI_POWER_CTRL  13

#define MIN_VUSB_MV         4750
#define MIN_VIN_MV          5000

#define MAX_POWER_LOW_COUNTER   0

#define DCDC_ON_DELAY_US    10000

#define HEARTBEAT_CHECK_INTERVAL_US     60000000

#define POWER_CYCLE_INTERVAL_US     1000000

#define SYSTEM_UP_MAX_DELAY_US      30000000


int power_low_counter = 0;

uint8_t current_rpi_state = STATE_OFF;

alarm_id_t heartbeat_missed_alarm_id = -1;

uint8_t heartbeat_missing_count = 0;

gpio_event_callback_t power_cut_callback = NULL;

alarm_id_t power_off_alarm_id = -1;

alarm_id_t system_up_alarm_id = -1;

alarm_id_t rpi_off_intermittent_task_alarm_id = -1;

uint8_t power_mode = POWER_MODE_NONE;

bool vin_recoverable = false;

uint8_t action_reason = 0;


// Callback when system up timeout
int64_t system_up_timeout_callback(alarm_id_t id, void *user_data) {
	control_led(false, 0);
    debug_log("Switch to ON state.\n");
    current_rpi_state = STATE_ON;
    system_up_alarm_id = -1;
    return 0;
}


// Callback when hearbeat is missed
int64_t heartbeat_missed_callback(alarm_id_t id, void *user_data) {
    if (current_rpi_state == STATE_STOPPING || current_rpi_state == STATE_OFF) {
        return 0;
    }
    uint8_t allowed = conf_get(CONF_WATCHDOG);
    if (allowed == 0) { // Watchdog is disabled
        return 0;
    }

    heartbeat_missing_count++;
    
    debug_log("Heartbeat is missed (%d/%d).\n", heartbeat_missing_count, allowed);
    
    if (heartbeat_missing_count > allowed) {

        debug_log("Missing too many heartbeats: power cycle is required.\n");

        request_shutdown(true, ACTION_REASON_MISSED_HEARTBEAT); // Make a power cycle

        heartbeat_missing_count = 0;
    }

    if (current_rpi_state == STATE_STARTING || current_rpi_state == STATE_ON) {
        return HEARTBEAT_CHECK_INTERVAL_US;
    }
    return 0;
}


/**
 * Reset the heartbeat checking timer when heartbeat arrives.
 */
void reset_heatbeat_checking_timer(void) {
    heartbeat_missing_count = 0;
    cancel_alarm(heartbeat_missed_alarm_id);
    heartbeat_missed_alarm_id = add_alarm_in_us(HEARTBEAT_CHECK_INTERVAL_US, heartbeat_missed_callback, NULL, true);
}


/**
 * Clear system up timer when heartbeat arrives.
 */
void clear_system_up_timer(void) {
	if (system_up_alarm_id != -1) {
		cancel_alarm(system_up_alarm_id);
		system_up_timeout_callback(system_up_alarm_id, NULL);
		system_up_alarm_id = -1;
	}
}


// Callback for running intermittent task, when Raspberry Pi is off
int64_t rpi_off_intermittent_task_callback(alarm_id_t id, void *user_data) {
	rpi_off_intermittent_task();
	uint8_t pulse_interval = conf_get(CONF_PULSE_INTERVAL);
	return (int64_t)pulse_interval * 1000000;
}


// Schedule the task when Raspberry Pi is off
void schedule_rpi_off_intermittent_task(void) {
	uint8_t pulse_interval = conf_get(CONF_PULSE_INTERVAL);
	cancel_alarm(rpi_off_intermittent_task_alarm_id);
	rpi_off_intermittent_task_alarm_id = add_alarm_in_us((int64_t)pulse_interval * 1000000, rpi_off_intermittent_task_callback, NULL, true);
}


/**
 * Initialize power manager
 */
void power_init(void) {
	gpio_init(GPIO_PI_HAS_3V3);
	gpio_set_dir(GPIO_PI_HAS_3V3, GPIO_IN);
	gpio_set_input_enabled(GPIO_PI_HAS_3V3, false); // Disable by default due to RP2350-E9

    gpio_init(GPIO_DCDC_ENABLE);
    gpio_set_dir(GPIO_DCDC_ENABLE, GPIO_OUT);
    gpio_put(GPIO_DCDC_ENABLE, false);

    gpio_init(GPIO_PI_POWER_CTRL);
    gpio_set_dir(GPIO_PI_POWER_CTRL, GPIO_OUT);
    gpio_put(GPIO_PI_POWER_CTRL, false);
    
    // Turn on DC/DC converter, if VIN has priority
    gpio_put(GPIO_DCDC_ENABLE, conf_get(CONF_PS_PRIORITY) == POWER_SOURCE_PRIORITY_VIN);
	
	schedule_rpi_off_intermittent_task();
}



/**
 * Controll Raspberry Pi's power
 * 
 * @param on true to power Raspberry Pi, false to cut power
 * @return true if action performed successfully, false otherwise
 */
bool power_control_pi_power(bool on) {
    if (on) {   // Try to power Raspberry Pi
        if (gpio_get(GPIO_PI_POWER_CTRL) == true) {
            debug_log("Can not turn on: Pi power is already on.\n");
            return false;
        }
        uint8_t priority = conf_get(CONF_PS_PRIORITY);
        if (priority == POWER_SOURCE_PRIORITY_VUSB) {   // VUSB has priority
            uint16_t vusb = get_vusb_mv();
            if (vusb >= MIN_VUSB_MV) {  // Vusb is high enough
                gpio_put(GPIO_DCDC_ENABLE, false);
                gpio_put(GPIO_PI_POWER_CTRL, true);
                power_mode = POWER_MODE_VUSB;
                current_rpi_state = STATE_STARTING;
                reset_heatbeat_checking_timer();
                debug_log("Raspberry Pi is powered by Vusb (%dmV).\n", vusb);
                return true;
            } else {  // Vusb is too low
                uint16_t vin = get_vin_mv();
                if (vin >= MIN_VIN_MV) {    // Vin is high enough
                    if (gpio_get(GPIO_DCDC_ENABLE) == false) {  // DC/DC is still off
                        gpio_put(GPIO_DCDC_ENABLE, true);
                        sleep_us(DCDC_ON_DELAY_US);
                    }
                    gpio_put(GPIO_PI_POWER_CTRL, true);
                    current_rpi_state = STATE_STARTING;
                    debug_log("Raspberry Pi is powered by Vin (%dmV).\n", vin);
                    
					power_mode = POWER_MODE_VIN;
                    reset_heatbeat_checking_timer();
                    return true;
    
                } else {    // Vin is also too low
					power_mode = POWER_MODE_NONE;
                    debug_log("Voltage is too low to power Raspberry Pi: Vusb=%dmV, Vin=%dmV\n", vusb, vin);
                    return false;
                }
            }
        } else if (priority == POWER_SOURCE_PRIORITY_VIN) { // VIN has priority
            // Make sure DC/DC is on  
			if (gpio_get(GPIO_DCDC_ENABLE) == false) {
				gpio_put(GPIO_DCDC_ENABLE, true);
				sleep_us(DCDC_ON_DELAY_US);
			}
			uint16_t vin = get_vin_mv();
			if (vin >= MIN_VIN_MV) {    // Vin is high enough
				gpio_put(GPIO_PI_POWER_CTRL, true);
				power_mode = POWER_MODE_VIN;
				current_rpi_state = STATE_STARTING;
				reset_heatbeat_checking_timer();
				debug_log("Raspberry Pi is powered by Vin (%dmV).\n", vin);
				return true;
			} else {	// Vin is too low
				uint16_t vusb = get_vusb_mv();
				if (vusb >= MIN_VUSB_MV) {  // Vusb is high enough
					gpio_put(GPIO_PI_POWER_CTRL, true);
					power_mode = POWER_MODE_VUSB;
					current_rpi_state = STATE_STARTING;
					reset_heatbeat_checking_timer();
					debug_log("Raspberry Pi is powered by Vusb (%dmV).\n", vusb);
					return true;
				} else {	// Vusb is also too low
					power_mode = POWER_MODE_NONE;
                    debug_log("Voltage is too low to power Raspberry Pi: Vin=%dmV, Vusb=%dmV\n", vin, vusb);
                    return false;
				}
			}
        } else {
			power_mode = POWER_MODE_NONE;
            debug_log("Unkown power source priority: %d\n", priority);
            return false;
        }
    } else {    // Try to cut Raspberry Pi's power
        if (gpio_get(GPIO_PI_POWER_CTRL) == false) {
            debug_log("Can not cut power: Pi is not powered.\n");
            return false;
        }
		request_rpi_shutdown(false);
        gpio_put(GPIO_PI_POWER_CTRL, false);
		gpio_put(GPIO_DCDC_ENABLE, conf_get(CONF_PS_PRIORITY) == POWER_SOURCE_PRIORITY_VIN);
		power_mode = POWER_MODE_NONE;
        current_rpi_state = STATE_OFF;
        debug_log("Switch to OFF state.\n");
        schedule_rpi_off_intermittent_task();
        if (power_cut_callback) {
            power_cut_callback();
        }
        return true;
    }
}


/**
 * Check whether Raspberry Pi is powered
 * 
 * @return true if Raspberry Pi is powered, false otherwise
 */
bool is_rpi_powered(void) {
    if (gpio_get(GPIO_PI_POWER_CTRL) == true) {
        return true;
    }
    return false;
}


/**
 * Request Raspberry Pi to startup
 * 
 * @param reason The reason for startup (defined by ACTION_REASON_??? macro)
 * @return true if request successfully, false otherwise
 */
bool request_startup(uint8_t reason) {
    if (current_rpi_state == STATE_OFF) {
        
        action_reason = (reason << 4) | (action_reason & 0xF);  // Set action reason
        
		apply_dst_if_needed();	// Apply DST if needed
		
		cancel_alarm(rpi_off_intermittent_task_alarm_id);
		control_led(true, 0);
        debug_log("Switch to STARTING state.\n");
        power_control_pi_power(true);
        system_up_alarm_id = add_alarm_in_us(SYSTEM_UP_MAX_DELAY_US, system_up_timeout_callback, NULL, true);
          
        if (!load_script(true)) {   // Schedule next shutdown
			load_and_schedule_alarm(false);
		}
    } else if (current_rpi_state == STATE_ON) {
        debug_log("Current state is already ON state.\n");
        gpio_put(GPIO_PI_POWER_CTRL, true);
    } else {
        debug_log("Can not request startup at this state: %d\n", current_rpi_state);
        return false;
    }
}


void restart_after_power_cut(void) {
    power_cut_callback = NULL;
    cancel_alarm(rpi_off_intermittent_task_alarm_id);
    sleep_us(POWER_CYCLE_INTERVAL_US);
    debug_log("Restart as previously requested.\n");
    request_startup(ACTION_REASON_REBOOT);
}


int64_t power_off_callback(alarm_id_t id, void *user_data) {
	control_led(false, 0);
    debug_log("Cut Raspberry Pi's power.\n");
    power_control_pi_power(false);
    power_off_alarm_id = -1;
    return 0;
}


/**
 * Request Raspberry Pi to shutdown
 *
 * @param restart Whether restart is needed
 * @param reason The reason for shutdown (defined by ACTION_REASON_??? macro)
 * @return true if request successfully, false otherwise
 */
bool request_shutdown(bool restart, uint8_t reason) {
    if (current_rpi_state == STATE_ON) {
        
        action_reason = (action_reason & 0xF0) | (reason & 0xF);  // Set action reason
        
		request_rpi_shutdown(true);
		control_led(true, 0);
        debug_log("Switch to STOPPING state.\n");
        current_rpi_state = STATE_STOPPING;
        power_cut_callback = restart ? restart_after_power_cut : NULL;
		uint64_t delay = (uint64_t)conf_get(CONF_POWER_CUT_DELAY) * 1000000;
        power_off_alarm_id = add_alarm_in_us(delay, power_off_callback, NULL, true);
        
        if (!restart) { // Schedule next startup
            if (!load_script(true)) {
    		    load_and_schedule_alarm(true);
    	    }
	    }
    } else if (current_rpi_state == STATE_OFF) {
        debug_log("Current state is already OFF state.\n");
        gpio_put(GPIO_PI_POWER_CTRL, false);
    } else if (current_rpi_state != STATE_STOPPING) {   // Software may inform the shutdown/reboot when state is STATE_STOPPING
        debug_log("Can not request shutdown at this state: %d\n", current_rpi_state);
        return false;
    }
}


/**
 * Get the current power mode
 *
 * @return POWER_MODE_VUSB or POWER_MODE_VIN
 */
uint8_t get_power_mode(void) {
    return power_mode;
}


/**
 * Intermittent task when Raspberry Pi is off.
 * Will blink LED and drive dummy load according to configuration.
 *
 * @return The configured time (in ms) for the task
 */
uint8_t rpi_off_intermittent_task(void) {
	uint8_t blink_led = conf_get(CONF_BLINK_LED);
	if (blink_led != 0) {
		control_led(true, blink_led);
	}
	uint8_t dummy_load = conf_get(CONF_DUMMY_LOAD);
	if (dummy_load != 0) {
		dummy_load_control(true, dummy_load);
	}
	return blink_led > dummy_load ? blink_led : dummy_load;
}


/**
 * Poll power sources and decide the current power mode.
 * Will also request startup/shutdown according to configuration.
 *
 * @return The integer as polling result
 */
int power_source_polling(void) {
    
	uint8_t priority = conf_get(CONF_PS_PRIORITY);
	
	if (gpio_get(GPIO_PI_POWER_CTRL) == true) {     // Raspberry Pi is powered
		
		if (priority == POWER_SOURCE_PRIORITY_VUSB) {   // VUSB has prioity
			
			uint16_t vusb = get_vusb_mv();
			
            if (vusb >= MIN_VUSB_MV) {  // Vusb is high enough
                gpio_put(GPIO_DCDC_ENABLE, false);
                power_mode = POWER_MODE_VUSB;
                power_low_counter = 0;
                return POWERED_BY_VUSB_NO_ACTION;
                
            } else {  // Vusb is too low
                
                uint16_t vin = get_vin_mv();
				uint16_t vlow = conf_get(CONF_LOW_VOLTAGE) * 100;
                
                if (vin >= MIN_VIN_MV && (vlow ==0 || vin >= vlow)) {    // Vin is high enough
                    if (gpio_get(GPIO_DCDC_ENABLE) == false) {
                        gpio_put(GPIO_DCDC_ENABLE, true);
                        sleep_us(DCDC_ON_DELAY_US);
                    }
					power_mode = POWER_MODE_VIN;
					power_low_counter = 0;
					return POWERED_BY_VIN_NO_ACTION;
					
                } else {    // Vin is also too low
                    power_low_counter ++;
                    if (power_low_counter > MAX_POWER_LOW_COUNTER) {
                        vin_recoverable = true;
                        debug_log("Power low: Vusb=%dmV, Vin=%dmV, Vlow=%dmV\n", vusb, vin, vlow);
    					request_shutdown(false, ACTION_REASON_VIN_DROP);
    					return POWER_LOW_SHUTDOWN;
				    }
				    return POWER_LOW_PENDING;
                }
            }
		} else if (priority == POWER_SOURCE_PRIORITY_VIN) { // VIN has priority
		    
		    if (gpio_get(GPIO_DCDC_ENABLE) == false) {
			    gpio_put(GPIO_DCDC_ENABLE, true);
			    sleep_us(DCDC_ON_DELAY_US);
		    }
			
			uint16_t vin = get_vin_mv();
			uint16_t vusb = get_vusb_mv();
			
			if (vin >= MIN_VIN_MV || vusb < MIN_VUSB_MV) {    // Vin is high enough, or Vusb is too low
				power_mode = POWER_MODE_VIN;
				uint16_t vlow = conf_get(CONF_LOW_VOLTAGE) * 100;
				if (vlow == 0 || vin >= vlow) {
				    power_low_counter = 0;
				    return POWERED_BY_VIN_NO_ACTION;
				} else {
				    power_low_counter ++;
				    if (power_low_counter > MAX_POWER_LOW_COUNTER) {
				        vin_recoverable = true;
    					debug_log("Power low: Vin=%dmV, Vlow=%dmV\n", vin, vlow);
    					request_shutdown(false, ACTION_REASON_VIN_DROP);
    					return POWER_LOW_SHUTDOWN;
				    }
				    return POWER_LOW_PENDING;
				}
			} else {	// Vin is not high enough and Vusb is high enough
			    power_mode = POWER_MODE_VUSB;
			    power_low_counter = 0;
			    return POWERED_BY_VUSB_NO_ACTION;
			}
		}
	} else {    // Raspberry Pi is not powered
		power_mode = POWER_MODE_NONE;
		
		if (can_vin_turn_on_rpi() && !can_cur_time_turn_off_rpi() && !can_temperature_turn_off_rpi()) {
		    debug_log("Startup occurs due to high Vin.\n");
			request_startup(ACTION_REASON_VIN_RECOVER);
			return POWER_RECOVER_STARTUP;
		}
		
		return PI_NOT_POWERED;
	}
}


/**
 * Check whether Raspberry Pi can be woke up by VIN (higher than recovery voltage)
 *
 * @return true or false
 */
bool can_vin_turn_on_rpi(void) {
    if (vin_recoverable) {
    	uint16_t vrec = conf_get(CONF_RECOVERY_VOLTAGE) * 100;
    	if (vrec != 0) {
    		uint16_t vin = get_vin_mv();
    		if (vin >= vrec) {
    			debug_log("Vin=%dmV, Vrec=%dmV\n", vin, vrec);
    			vin_recoverable = false;
    			return true;
    		}
    	}
    }
	return false;
}


/**
 * Check whether Raspberry Pi can be shut down by VIN (lower than low voltage)
 * The checking is skipped if power mode is not POWER_MODE_VIN
 *
 * @return true or false
 */
bool can_vin_turn_off_rpi(void) {
	if (power_mode == POWER_MODE_VIN) {
		uint16_t vlow = conf_get(CONF_LOW_VOLTAGE) * 100;
		if (vlow != 0) {
			uint16_t vin = get_vin_mv();
			if (vin < vlow) {
				debug_log("Vin=%dmV, Vlow=%dmV\n", vin, vlow);
				return true;
			}
		}
	}
	return false;
}


/**
 * Check whether Raspberry Pi's 3.3V is currently ON
 *
 * @return true or false
 */
bool is_rpi_3V3_on(void) {
    // Temporarily enable the input to read voltage level on RPi's 3V3
    gpio_set_input_enabled(GPIO_PI_HAS_3V3, true);
    bool state = gpio_get(GPIO_PI_HAS_3V3);
    // Disable the input after usage, due to RP2350-E9
    gpio_set_input_enabled(GPIO_PI_HAS_3V3, false);
    return state;
}


/**
 * Get the latest action reason
 *
 * @return The latest action reason. Higher 4 bits for startup and lower 4 bits for shutdown.
 */
uint8_t get_action_reason(void) {
    return action_reason;
}