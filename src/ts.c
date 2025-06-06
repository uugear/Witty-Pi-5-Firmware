#include <stdint.h>
#include <string.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>

#include "ts.h"
#include "i2c.h"
#include "log.h"
#include "conf.h"


#define SMBUS_ALERT_RESPONSE_ADDRESS    0x0C

// Alert status (seems opposite with section 7.3.2.5 in TMP112 datasheet)
#define ALERT_STATUS_OVER_TEMPERATURE   0
#define ALERT_STATUS_BELOW_TEMPERATURE  1


gpio_event_callback_t below_callback = NULL;
gpio_event_callback_t over_callback = NULL;


// Get SMBus alert status
//
// @return The status, -1 if error
int get_smbus_alert_status() {
    uint8_t received_byte;
    int ret = i2c_read_blocking(i2c0, SMBUS_ALERT_RESPONSE_ADDRESS, &received_byte, 1, true);
    if (ret < 0) {
        return -1;
    }
    uint8_t received_address_7bit = (received_byte >> 1);
    if (received_address_7bit != TMP112_ADDRESS) {
        return -1;
    }
    return received_byte & 0x01;
}


// Callback when alert occurs
void ts_alert_callback(void) {
    int status = -1;
    for (int i = 0; status == -1 && i < 5; i ++) {
        status = get_smbus_alert_status();
    }
    if (status == -1) {
        debug_log("Received alert without status.\n");
        return; // Ignore alert without smbus response
    }

    // Read temperature to clear alert status
    int32_t t = ts_get_temperature_mc();
    int32_t t_low = ts_get_t_low_mc();
    int32_t t_high = ts_get_t_high_mc();
    gpio_set_input_enabled(GPIO_TS_INT, false);
    debug_log("t=%d, t_low=%d, t_high=%d, status=%d\n", t, t_low, t_high, status);
    gpio_set_input_enabled(GPIO_TS_INT, true);

    // Invoke callback according to alert status
    if (status == ALERT_STATUS_OVER_TEMPERATURE) {
        if (over_callback) {
            over_callback();
        }
    } else if (status == ALERT_STATUS_BELOW_TEMPERATURE) {
        if (below_callback) {
            below_callback();
        }
    } else {
        debug_log("Unknown alert status: %d\n", status);
    }
}


// Extra processing after configuration is changed
void on_temp_point_conf_changed(const char *key, uint8_t old_val, uint8_t new_val) {
	if (strcmp(key, CONF_BELOW_TEMP_POINT) == 0) {
		ts_set_t_low_mc((int32_t)new_val * 1000);
	} else if (strcmp(key, CONF_OVER_TEMP_POINT) == 0) {
		ts_set_t_high_mc((int32_t)new_val * 1000);
	}
}


/**
 * Initialize the temperature sensor
 *
 * @param below Callback when temperature is below T_low
 * @param over Callback when temperature is over T_high
 */
void ts_init(gpio_event_callback_t below, gpio_event_callback_t over) {
	set_virtual_register(I2C_VREG_TMP112_CONF_MSB, 0x7A);
	set_virtual_register(I2C_VREG_TMP112_CONF_LSB, 0xA0);

    gpio_init(GPIO_TS_INT);
    gpio_set_dir(GPIO_TS_INT, GPIO_IN);
    gpio_pull_up(GPIO_TS_INT);

    below_callback = below;
    over_callback = over;
    gpio_register_callback(GPIO_TS_INT, GPIO_IRQ_EDGE_FALL, ts_alert_callback);

	ts_set_t_low_mc((int32_t)conf_get(CONF_BELOW_TEMP_POINT) * 1000);
	register_item_changed_callback(CONF_BELOW_TEMP_POINT, on_temp_point_conf_changed);

	ts_set_t_high_mc((int32_t)conf_get(CONF_OVER_TEMP_POINT) * 1000);
	register_item_changed_callback(CONF_OVER_TEMP_POINT, on_temp_point_conf_changed);
}


// Convert the 12 bits data in MSB and LSB to millidegree Celsius
int32_t get_12bits_temperature_mc(uint8_t msb, uint8_t lsb) {
	int16_t raw = (msb << 4) | (lsb >> 4);
    if (raw & 0x800) {
        raw |= 0xF000;
    }
    return (int32_t)raw * 625 / 10;
}


// Convert millidegree Celsius data to MSB and LSB
void get_msb_lsb(int32_t temp, uint8_t *msb, uint8_t *lsb) {
    int16_t raw = (int16_t)((temp * 10) / 625);
    if (raw > 2047) {
        raw = 2047;
    } else if (raw < -2048) {
        raw = -2048;
    }
	if (msb) {
		*msb = (uint8_t)((raw >> 4) & 0xFF);
	}
	if (lsb) {
		*lsb = (uint8_t)((raw & 0x0F) << 4);
	}
}


/**
 * Get temperature in millidegree Celsius
 *
 * @return the temperature in millidegree Celsius
 */
int32_t ts_get_temperature_mc(void) {
    uint8_t msb = get_virtual_register(I2C_VREG_TMP112_TEMP_MSB);
    uint8_t lsb = get_virtual_register(I2C_VREG_TMP112_TEMP_LSB);
	return get_12bits_temperature_mc(msb, lsb);
}


/**
 * Get T_low in millidegree Celsius
 *
 * @return the temperature in millidegree Celsius
 */
int32_t ts_get_t_low_mc(void) {
    uint8_t msb = get_virtual_register(I2C_VREG_TMP112_TLOW_MSB);
    uint8_t lsb = get_virtual_register(I2C_VREG_TMP112_TLOW_LSB);
	return get_12bits_temperature_mc(msb, lsb);
}


/**
 * Set T_low in millidegree Celsius
 *
 * @param tl The temperature in millidegree Celsius
 */
void ts_set_t_low_mc(int32_t tl) {
    uint8_t msb;
    uint8_t lsb;
	get_msb_lsb(tl, &msb, &lsb);
	set_virtual_register(I2C_VREG_TMP112_TLOW_MSB, msb);
	set_virtual_register(I2C_VREG_TMP112_TLOW_LSB, lsb);
}


/**
 * Get T_high in millidegree Celsius
 *
 * @return the temperature in millidegree Celsius
 */
int32_t ts_get_t_high_mc(void) {
    uint8_t msb = get_virtual_register(I2C_VREG_TMP112_THIGH_MSB);
    uint8_t lsb = get_virtual_register(I2C_VREG_TMP112_THIGH_LSB);
	return get_12bits_temperature_mc(msb, lsb);
}


/**
 * Set T_high in millidegree Celsius
 *
 * @param th The temperature in millidegree Celsius
 */
void ts_set_t_high_mc(int32_t th) {
    uint8_t msb;
    uint8_t lsb;
	get_msb_lsb(th, &msb, &lsb);
	set_virtual_register(I2C_VREG_TMP112_THIGH_MSB, msb);
	set_virtual_register(I2C_VREG_TMP112_THIGH_LSB, lsb);
}


/**
 * Check whether Raspberry Pi can be shut down by temperature
 *
 * @return true of false
 */
bool can_temperature_turn_off_rpi(void) {
	uint8_t over_action = conf_get(CONF_OVER_TEMP_ACTION);
	uint8_t below_action = conf_get(CONF_BELOW_TEMP_ACTION);
	if (over_action == TEMP_ACTION_SHUTDOWN || below_action == TEMP_ACTION_SHUTDOWN) {
		int32_t temp = ts_get_temperature_mc() / 1000;
		if (over_action == TEMP_ACTION_SHUTDOWN) {
			int8_t over_point = (int8_t)conf_get(CONF_OVER_TEMP_POINT);
			if (temp > over_point) {
				return true;
			}
		}
		if (below_action == TEMP_ACTION_SHUTDOWN) {
			int8_t below_point = (int8_t)conf_get(CONF_BELOW_TEMP_POINT);
			if (temp < below_point) {
				return true;
			}
		}
	}
	return false;
}
