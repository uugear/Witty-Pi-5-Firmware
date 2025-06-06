#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/binary_info.h>
#include <hardware/i2c.h>
#include <hardware/powman.h>
#include <pico/i2c_slave.h>
#include <tusb.h>

#include "i2c.h"
#include "adc.h"
#include "log.h"
#include "main.h"
#include "flash.h"
#include "fatfs_disk.h"
#include "conf.h"
#include "rtc.h"
#include "power.h"
#include "led.h"
#include "id_eeprom.h"
#include "script.h"
#include "util.h"


#define PRODUCT_INFO_STR        PRODUCT_NAME " (Firmware: V" TO_STRING(FIRMWARE_VERSION_MAJOR) "." TO_STRING(FIRMWARE_VERSION_MINOR) ")\n"

#define I2C_MASTER_SDA_PIN 		4
#define I2C_MASTER_SCL_PIN		5

#define I2C_SLAVE_SDA_PIN		6
#define I2C_SLAVE_SCL_PIN		7
#define I2C_SLAVE_ADDRESS		0x51

#define I2C_MASTER_BAUDRATE     400000	// 400 kHz
#define I2C_SLAVE_BAUDRATE      100000	// 100 kHz

#define TMP112_REG_TEMP			0
#define TMP112_REG_CONF			1
#define TMP112_REG_TLOW			2
#define TMP112_REG_THIGH		3

#define ADMIN_TURN_RPI_OFF      1
#define ADMIN_RPI_POWERING_OFF  2
#define ADMIN_RPI_REBOOTING     3

#define CRC8_POLYNOMIAL			0x31	// CRC-8 Polynomial (x^8 + x^5 + x^4 + 1 -> 00110001 -> 0x31)
#define DOWNLOAD_BUFFER_SIZE	1024
#define UPLOAD_BUFFER_SIZE	    1024

#define DIRECTORY_COUNT         4


int i2c_index = -1;
uint8_t i2c_cached_index;
uint8_t i2c_admin_reg[16] = {0};

uint8_t vusb_msb;
uint8_t vusb_lsb;
uint8_t vin_msb;
uint8_t vin_lsb;
uint8_t vout_msb;
uint8_t vout_lsb;
uint8_t iout_msb;
uint8_t iout_lsb;

uint8_t temp_msb;
uint8_t temp_lsb;
uint8_t conf_msb;
uint8_t conf_lsb;
uint8_t tlow_msb;
uint8_t tlow_lsb;
uint8_t thigh_msb;
uint8_t thigh_lsb;

extern uint8_t heartbeat_missing_count;

uint8_t download_buffer[DOWNLOAD_BUFFER_SIZE];
int download_buffer_index = 0;

uint8_t upload_buffer[UPLOAD_BUFFER_SIZE];
int upload_buffer_index = 0;

const char *dir_names[DIRECTORY_COUNT + 1] = {
    NULL,
    "/",
    "/conf",
	"/log",
	"/schedule"
};

uint64_t heartbeat_update_time = 0;


/**
 * Calculates the CRC-8 checksum for a data buffer
 *
 * @param data Pointer to the data buffer
 * @param len Length of the data buffer (in bytes)
 * @return uint8_t The calculated CRC-8 checksum (single byte)
 */
uint8_t calculate_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        // XOR the current data byte with the current CRC value
        crc ^= data[i];
        // Perform 8 shifts and XOR operations (simulate polynomial division)
        for (int j = 0; j < 8; j++) {
            // Check the most significant bit (MSB) of the current CRC value
            if ((crc & 0x80) != 0) {
                // If MSB is 1, left shift CRC by 1 and XOR with the polynomial
                crc = (crc << 1) ^ CRC8_POLYNOMIAL;
            } else {
                // If MSB is 0, just left shift CRC by 1
                crc <<= 1;
            }
        }
    }
    return crc;
}


// Prepare the file list for directory and put it into the download buffer
void pack_file_list(int dir) {
    if (dir > 0 && dir <= DIRECTORY_COUNT) {
        int index = 0;
        download_buffer[index++] = PACKET_BEGIN;
        bool sp = true;
        FRESULT fr;
        DIR dj;
        FILINFO fno;
        fr = f_opendir(&dj, dir_names[dir]);
        if (fr == FR_OK) {
            debug_log("Listing files in directory: %s\n", dir_names[dir]);
            while (true) {
                fr = f_readdir(&dj, &fno);
                if (fr != FR_OK || fno.fname[0] == 0) {
                    break;
                }
                if (!(fno.fattrib & AM_DIR)) {
                    if (fno.fname[0] == '.') {
                        continue; // Skip file name starts with '.'
                    }
                    if (!sp) {
                        download_buffer[index++] = PACKET_DELIMITER;
                    }
                    int len = strlen(fno.fname);
                    if (index + len + 3 >= DOWNLOAD_BUFFER_SIZE) { // +3 for delimiter, CRC, END
                        debug_log("Buffer is full and skip 1 or more files.\n");
                        break;
                    }
                    strncpy((char *)(download_buffer + index), fno.fname, len);
                    index += len;
                    sp = false;
                }
            }
            f_closedir(&dj);

            uint8_t crc = calculate_crc8(download_buffer, index);
            download_buffer[index++] = PACKET_DELIMITER;
            download_buffer[index++] = crc;
            download_buffer[index++] = PACKET_END;

            if (index < DOWNLOAD_BUFFER_SIZE) {
                download_buffer[index] = '\0';
            } else {
                download_buffer[DOWNLOAD_BUFFER_SIZE - 1] = '\0';
            }
        } else {
            debug_log("Failed to open directory %s. Error code: %d\n", dir_names[dir], fr);
        }

        download_buffer_index = 0;
    }
}


// Extract file name from packet and save to buffer
bool unpack_filename(char* input, char* output) {
    if (input == NULL || output == NULL) {
        return false;
    }
    char* start = strchr(input, PACKET_BEGIN);
    if (start == NULL) {
        return false;
    }
    start++;
    char* end = strchr(start, PACKET_END);
    if (end == NULL) {
        return false;
    }
    char* delimiter = strchr(start, PACKET_DELIMITER);
    if (delimiter == NULL || delimiter >= end) {
        return false;
    }
    int filename_len = delimiter - start;
    if (filename_len <= 0) {
        return false;
    }
    strncpy(output, start, filename_len);
    output[filename_len] = '\0';
    return true;
}


// Callback for applying schedule script
int64_t apply_schedule_script_callback(alarm_id_t id, void *user_data) {
	if (load_script(true)) {
        debug_log("Load and run script OK\n");
    } else {
        debug_log("Load and run script failed\n");
    }
    return 0;
}


// Apply the schedule script
bool apply_schedule_script(int dir, char* filename) {
    if (dir > 0 && dir <= DIRECTORY_COUNT) {
        char buf[256];
        sprintf(buf, "%s/%s", dir_names[dir], filename);
        if (file_exists(buf)) {
            purge_script();
            const char *ext = strrchr(filename, '.');
            char * dest = NULL;
            if (ext) {
                if (strcasecmp(ext, ".wpi") == 0) {
                    dest = WPI_SCRIPT_PATH;
                } else if (strcasecmp(ext, ".act") == 0) {
                    dest = ACT_SCRIPT_PATH;
                } else if (strcasecmp(ext, ".skd") == 0) {
                    dest = SKD_SCRIPT_PATH;
                }
            }
            if (dest && file_copy(dest, buf)) {
                set_script_in_use(true);
                add_alarm_in_us(500000, apply_schedule_script_callback, NULL, true);
                return true;
            } else {
                debug_log("Failed to copy script: %s\n", buf);
            }
        } else {
            debug_log("Script file does not exist: %s\n", buf);
        }
    }
    return false;
}


/**
 * Run administrative command
 *
 * @param pwd The password
 * @param cmd The command
 */
void run_admin_command() {
	uint8_t pwd = i2c_admin_reg[I2C_ADMIN_PASSWORD - I2C_ADMIN_FIRST];
	uint8_t cmd = i2c_admin_reg[I2C_ADMIN_COMMAND - I2C_ADMIN_FIRST];
	uint16_t pwd_cmd = ((uint16_t)pwd << 8) | cmd;
	switch (pwd_cmd) {
	    case I2C_ADMIN_PWD_CMD_PRINT_PRODUCT_INFO:  // Print product name and firmware version
			debug_log("Admin CMD: Print Product Info\n");
	        debug_log("%s\n", PRODUCT_INFO_STR);
	        break;
        case I2C_ADMIN_PWD_CMD_FORMAT_DISK:         // Format the disk (all data will be gone!)
            debug_log("Admin CMD: Format Disk\n");
            tud_msc_start_stop_cb(0, 0, false, true);
            unmount_fatfs();
            flash_fatfs_init();
			mount_fatfs();
            create_default_dirs();
            break;
        case I2C_ADMIN_PWD_CMD_RESET_RTC:           // Reset RTC (time will lose!)
            debug_log("Admin CMD: Reset RTC\n");
            set_virtual_register(I2C_VREG_RX8025_CONTROL_REGISTER, BIT_VALUE(0));
            rtc_set_timestamp(0);
            break;
        case I2C_ADMIN_PWD_CMD_ENABLE_ID_EEPROM_WP: // Enable ID EEPROM write protection
    		debug_log("Admin CMD: Enable ID EEPROM WP\n");
    		id_eeprom_write_protection(true);
    		break;
        case I2C_ADMIN_PWD_CMD_DISABLE_ID_EEPROM_WP:// Disable ID EEPROM write protection
    		debug_log("Admin CMD: Disable ID EEPROM WP\n");
    		id_eeprom_write_protection(false);
    		break;
    	case I2C_ADMIN_PWD_CMD_RESET_CONF:          // Reset configuration to default values
    	    debug_log("Admin CMD: Reset Conf\n");
    	    conf_reset();
    	    break;
    	case I2C_ADMIN_PWD_CMD_SYNC_CONF:           // Synchronize configuration to file
    	    debug_log("Admin CMD: Sync Conf\n");
    	    tud_msc_start_stop_cb(0, 0, false, true);
    	    conf_sync();
    	    break;
    	case I2C_ADMIN_PWD_CMD_SAVE_LOG:            // Save log to file
    	    debug_log("Admin CMD: Save Log\n");
    	    if (is_log_saving_to_file()) {
                save_logs_to_file();
            }
			break;
        case I2C_ADMIN_PWD_CMD_LOAD_SCRIPT:         // Load and generate schedule script files
    	    debug_log("Admin CMD: Load Script\n");
    	    load_script(false);
    	    break;
    	case I2C_ADMIN_PWD_CMD_LIST_FILES:          // List files in specific directory
    	    debug_log("Admin CMD: List Files\n");
    	    pack_file_list(i2c_admin_reg[I2C_ADMIN_DIR - I2C_ADMIN_FIRST]);
    	    break;
    	case I2C_ADMIN_PWD_CMD_CHOOSE_SCRIPT:       // Choose schedule script
    	    debug_log("Admin CMD: Choose Script\n");
    	    tud_msc_start_stop_cb(0, 0, false, true);
    	    unpack_filename(upload_buffer, upload_buffer);
    	    debug_log("Applying script %s...\n", upload_buffer);
    	    apply_schedule_script(i2c_admin_reg[I2C_ADMIN_DIR - I2C_ADMIN_FIRST], upload_buffer);
    	    break;
    	case I2C_ADMIN_PWD_CMD_PURGE_SCRIPT:        // Purge schedule script
    	    debug_log("Admin CMD: Purge Script\n");
    	    purge_script();
    	    break;
    	default:
    	    debug_log("Unknown admin command: pwd=0x%02x, cmd=0x%02x\n", pwd, cmd);
	}
	// Clear the password and command
	i2c_admin_reg[I2C_ADMIN_PASSWORD - I2C_ADMIN_FIRST] = 0;
	i2c_admin_reg[I2C_ADMIN_COMMAND - I2C_ADMIN_FIRST] = 0;
}


/**
 * Get value from configuration register
 *
 * @param index The index of the register
 * @return The value of the register
 */
uint8_t get_config_register(uint8_t index) {

    switch (index) {
        case I2C_CONF_ADDRESS:
            return conf_get(CONF_ADDRESS);

        case I2C_CONF_DEFAULT_ON_DELAY:
            return conf_get(CONF_DEFAULT_ON_DELAY);
        case I2C_CONF_POWER_CUT_DELAY:
            return conf_get(CONF_POWER_CUT_DELAY);

        case I2C_CONF_PULSE_INTERVAL:
            return conf_get(CONF_PULSE_INTERVAL);
        case I2C_CONF_BLINK_LED:
            return conf_get(CONF_BLINK_LED);
        case I2C_CONF_DUMMY_LOAD:
            return conf_get(CONF_DUMMY_LOAD);

        case I2C_CONF_LOW_VOLTAGE:
            return conf_get(CONF_LOW_VOLTAGE);
        case I2C_CONF_RECOVERY_VOLTAGE:
            return conf_get(CONF_RECOVERY_VOLTAGE);

        case I2C_CONF_PS_PRIORITY:
            return conf_get(CONF_PS_PRIORITY);

        case I2C_CONF_ADJ_VUSB:
            return conf_get(CONF_ADJ_VUSB);
        case I2C_CONF_ADJ_VIN:
            return conf_get(CONF_ADJ_VIN);
        case I2C_CONF_ADJ_VOUT:
            return conf_get(CONF_ADJ_VOUT);
        case I2C_CONF_ADJ_IOUT:
            return conf_get(CONF_ADJ_IOUT);

        case I2C_CONF_WATCHDOG:
            return conf_get(CONF_WATCHDOG);

        case I2C_CONF_LOG_TO_FILE:
            return conf_get(CONF_LOG_TO_FILE);

        case I2C_CONF_BOOTSEL_FTY_RST:
            return conf_get(CONF_BOOTSEL_FTY_RST);

        case I2C_CONF_ALARM1_SECOND:
            return conf_get(CONF_ALARM1_SECOND);
        case I2C_CONF_ALARM1_MINUTE:
            return conf_get(CONF_ALARM1_MINUTE);
        case I2C_CONF_ALARM1_HOUR:
            return conf_get(CONF_ALARM1_HOUR);
        case I2C_CONF_ALARM1_DAY:
            return conf_get(CONF_ALARM1_DAY);

        case I2C_CONF_ALARM2_SECOND:
            return conf_get(CONF_ALARM2_SECOND);
        case I2C_CONF_ALARM2_MINUTE:
            return conf_get(CONF_ALARM2_MINUTE);
        case I2C_CONF_ALARM2_HOUR:
            return conf_get(CONF_ALARM2_HOUR);
        case I2C_CONF_ALARM2_DAY:
            return conf_get(CONF_ALARM2_DAY);

        case I2C_CONF_BELOW_TEMP_ACTION:
            return conf_get(CONF_BELOW_TEMP_ACTION);
        case I2C_CONF_BELOW_TEMP_POINT:
            return conf_get(CONF_BELOW_TEMP_POINT);
        case I2C_CONF_OVER_TEMP_ACTION:
            return conf_get(CONF_OVER_TEMP_ACTION);
        case I2C_CONF_OVER_TEMP_POINT:
            return conf_get(CONF_OVER_TEMP_POINT);

        case I2C_CONF_DST_OFFSET:
            return conf_get(CONF_DST_OFFSET);
        case I2C_CONF_DST_BEGIN_MON:
            return conf_get(CONF_DST_BEGIN_MON);
        case I2C_CONF_DST_BEGIN_DAY:
            return conf_get(CONF_DST_BEGIN_DAY);
        case I2C_CONF_DST_BEGIN_HOUR:
            return conf_get(CONF_DST_BEGIN_HOUR);
        case I2C_CONF_DST_BEGIN_MIN:
            return conf_get(CONF_DST_BEGIN_MIN);
        case I2C_CONF_DST_END_MON:
            return conf_get(CONF_DST_END_MON);
        case I2C_CONF_DST_END_DAY:
            return conf_get(CONF_DST_END_DAY);
        case I2C_CONF_DST_END_HOUR:
            return conf_get(CONF_DST_END_HOUR);
        case I2C_CONF_DST_END_MIN:
            return conf_get(CONF_DST_END_MIN);
        case I2C_CONF_DST_APPLIED:
            return conf_get(CONF_DST_APPLIED);

		case I2C_CONF_SYS_CLOCK_MHZ:
			return conf_get(CONF_SYS_CLOCK_MHZ);
    }
    return 0;
}


/**
 * Set value to configuration register
 *
 * @param index The index of the register
 * @param value The value to set
 */
void set_config_register(uint8_t index, uint8_t value) {

    switch (index) {
        case I2C_CONF_ADDRESS:
            conf_set(CONF_ADDRESS, value);
            break;

        case I2C_CONF_DEFAULT_ON_DELAY:
            conf_set(CONF_DEFAULT_ON_DELAY, value);
			break;
        case I2C_CONF_POWER_CUT_DELAY:
            conf_set(CONF_POWER_CUT_DELAY, value);
			break;

        case I2C_CONF_PULSE_INTERVAL:
            conf_set(CONF_PULSE_INTERVAL, value);
			break;
        case I2C_CONF_BLINK_LED:
            conf_set(CONF_BLINK_LED, value);
			break;
        case I2C_CONF_DUMMY_LOAD:
            conf_set(CONF_DUMMY_LOAD, value);
			break;

        case I2C_CONF_LOW_VOLTAGE:
            conf_set(CONF_LOW_VOLTAGE, value);
			break;
        case I2C_CONF_RECOVERY_VOLTAGE:
            conf_set(CONF_RECOVERY_VOLTAGE, value);
			break;

	    case I2C_CONF_PS_PRIORITY:
	        conf_set(CONF_PS_PRIORITY, value);
	        break;

        case I2C_CONF_ADJ_VUSB:
            conf_set(CONF_ADJ_VUSB, value);
			break;
        case I2C_CONF_ADJ_VIN:
            conf_set(CONF_ADJ_VIN, value);
			break;
        case I2C_CONF_ADJ_VOUT:
            conf_set(CONF_ADJ_VOUT, value);
			break;
        case I2C_CONF_ADJ_IOUT:
            conf_set(CONF_ADJ_IOUT, value);
			break;

        case I2C_CONF_WATCHDOG:
            conf_set(CONF_WATCHDOG, value);
            break;

        case I2C_CONF_LOG_TO_FILE:
            conf_set(CONF_LOG_TO_FILE, value);
			break;

        case I2C_CONF_BOOTSEL_FTY_RST:
            conf_set(CONF_BOOTSEL_FTY_RST, value);
			break;

        case I2C_CONF_ALARM1_SECOND:
            conf_set(CONF_ALARM1_SECOND, value);
			break;
        case I2C_CONF_ALARM1_MINUTE:
            conf_set(CONF_ALARM1_MINUTE, value);
			break;
        case I2C_CONF_ALARM1_HOUR:
            conf_set(CONF_ALARM1_HOUR, value);
			break;
        case I2C_CONF_ALARM1_DAY:
            conf_set(CONF_ALARM1_DAY, value);
			break;

        case I2C_CONF_ALARM2_SECOND:
            conf_set(CONF_ALARM2_SECOND, value);
			break;
        case I2C_CONF_ALARM2_MINUTE:
            conf_set(CONF_ALARM2_MINUTE, value);
			break;
        case I2C_CONF_ALARM2_HOUR:
            conf_set(CONF_ALARM2_HOUR, value);
			break;
        case I2C_CONF_ALARM2_DAY:
            conf_set(CONF_ALARM2_DAY, value);
			break;

        case I2C_CONF_BELOW_TEMP_ACTION:
            conf_set(CONF_BELOW_TEMP_ACTION, value);
			break;
        case I2C_CONF_BELOW_TEMP_POINT:
            conf_set(CONF_BELOW_TEMP_POINT, value);
			break;
        case I2C_CONF_OVER_TEMP_ACTION:
            conf_set(CONF_OVER_TEMP_ACTION, value);
			break;
        case I2C_CONF_OVER_TEMP_POINT:
            conf_set(CONF_OVER_TEMP_POINT, value);
			break;

        case I2C_CONF_DST_OFFSET:
            conf_set(CONF_DST_OFFSET, value);
            break;
        case I2C_CONF_DST_BEGIN_MON:
            conf_set(CONF_DST_BEGIN_MON, value);
            break;
        case I2C_CONF_DST_BEGIN_DAY:
            conf_set(CONF_DST_BEGIN_DAY, value);
            break;
        case I2C_CONF_DST_BEGIN_HOUR:
            conf_set(CONF_DST_BEGIN_HOUR, value);
            break;
        case I2C_CONF_DST_BEGIN_MIN:
            conf_set(CONF_DST_BEGIN_MIN, value);
            break;
        case I2C_CONF_DST_END_MON:
            conf_set(CONF_DST_END_MON, value);
            break;
        case I2C_CONF_DST_END_DAY:
            conf_set(CONF_DST_END_DAY, value);
            break;
        case I2C_CONF_DST_END_HOUR:
            conf_set(CONF_DST_END_HOUR, value);
            break;
        case I2C_CONF_DST_END_MIN:
            conf_set(CONF_DST_END_MIN, value);
            break;
        case I2C_CONF_DST_APPLIED:
            conf_set(CONF_DST_APPLIED, value);
            break;

		case I2C_CONF_SYS_CLOCK_MHZ:
			conf_set(CONF_SYS_CLOCK_MHZ, value);
			break;
    }
}


/**
 * Get value from read-only register
 *
 * @param index The index of the register
 * @return The register value, or 0 for error
 */
uint8_t get_read_only_register(uint8_t index) {
    uint8_t data = 0x00;
    switch (index) {
        case I2C_FW_ID:
            return FIRMWARE_ID;
        case I2C_FW_VERSION_MAJOR:
            return FIRMWARE_VERSION_MAJOR;
        case I2C_FW_VERSION_MINOR:
            return FIRMWARE_VERSION_MINOR;
        case I2C_VUSB_MV_MSB:
            read_voltage_mv(0, &vusb_msb, &vusb_lsb);
            return vusb_msb;
        case I2C_VUSB_MV_LSB:
            return vusb_lsb;
        case I2C_VIN_MV_MSB:
            read_voltage_mv(1, &vin_msb, &vin_lsb);
            return vin_msb;
        case I2C_VIN_MV_LSB:
            return vin_lsb;
        case I2C_VOUT_MV_MSB:
            read_voltage_mv(2, &vout_msb, &vout_lsb);
            return vout_msb;
        case I2C_VOUT_MV_LSB:
            return vout_lsb;
        case I2C_IOUT_MA_MSB:
            read_current_ma(3, &iout_msb, &iout_lsb);
            return iout_msb;
        case I2C_IOUT_MA_LSB:
            return iout_lsb;
        case I2C_POWER_MODE:
            return get_power_mode();
        case I2C_MISSED_HEARTBEAT:
            return heartbeat_missing_count;
        case I2C_RPI_STATE:
            return current_rpi_state;
        case I2C_ACTION_REASON:
            return get_action_reason();
		case I2C_MISC:
			return is_script_in_use();
    }
	return data;
}


//-----------------------------------------------------------------------------
//
// Handler for slave device connected to Raspberry Pi
//
//-----------------------------------------------------------------------------
static void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    switch (event) {
    case I2C_SLAVE_RECEIVE: // Master has written some data to this slave device

        if (i2c_index == -1) {
            // Master writes register index
            int old_i2c_index = i2c_index;
			i2c_index = i2c_read_byte_raw(i2c);

			if (i2c_index >= I2C_VREG_RX8025_SEC && i2c_index <= I2C_VREG_RX8025_CONTROL_REGISTER) {        // Index for accessing RTC
				uint8_t index = i2c_index - I2C_VREG_RX8025_SEC;
				i2c_write_burst_blocking(i2c0, RX8025_ADDRESS, &index, 1);
			} else if (i2c_index >= I2C_VREG_TMP112_TEMP_MSB && i2c_index <= I2C_VREG_TMP112_THIGH_LSB) {   // Index for accessing temperature sensor
				uint8_t index = (i2c_index - I2C_VREG_TMP112_TEMP_MSB) / 2;
				switch (i2c_index) {
					case I2C_VREG_TMP112_TEMP_MSB:
					case I2C_VREG_TMP112_CONF_MSB:
					case I2C_VREG_TMP112_TLOW_MSB:
					case I2C_VREG_TMP112_THIGH_MSB:
						i2c_write_burst_blocking(i2c0, TMP112_ADDRESS, &index, 1);
						break;
					default:
						break;
				}
			}
        } else {
            // Master writes register value
            uint8_t data = i2c_read_byte_raw(i2c);

			if (i2c_index >= I2C_CONF_FIRST && i2c_index <= I2C_CONF_LAST) {	        // Write [Configuration register]
				set_config_register(i2c_index, data);
			} else if (i2c_index >= I2C_ADMIN_FIRST && i2c_index <= I2C_ADMIN_LAST) {   // Write [Admin register]
				uint8_t old_value = i2c_admin_reg[i2c_index - I2C_ADMIN_FIRST];
				i2c_admin_reg[i2c_index - I2C_ADMIN_FIRST] = data;
				switch (i2c_index) {
					case I2C_ADMIN_COMMAND:     // Recived Admin command
						run_admin_command();
						break;
					case I2C_ADMIN_HEARTBEAT:   // Explicitly received heartbeat
						if (old_value != data) {
							reset_heatbeat_checking_timer();
							clear_system_up_timer();
						}
						break;
					case I2C_ADMIN_SHUTDOWN:    // Received shutdown/reboot request
					    if (data == ADMIN_RPI_POWERING_OFF)  {
					        request_shutdown(false, ACTION_REASON_EXTERNAL_SHUTDOWN);
					    } else if (data == ADMIN_RPI_REBOOTING)  {
					        request_shutdown(true, ACTION_REASON_EXTERNAL_REBOOT);
					    }
						break;
					case I2C_ADMIN_DIR:         // Set directory
					    download_buffer_index = 0;
					    upload_buffer_index = 0;
					    break;
					case I2C_ADMIN_UPLOAD:      // Master uploads something
					    upload_buffer[upload_buffer_index ++] = data;
					    if (data == PACKET_END) {
					        upload_buffer[upload_buffer_index ++] = '\0';
					    }
					    break;
				}
			} else {    // Write [Virtual registers]
				if (i2c_index >= I2C_VREG_RX8025_SEC && i2c_index <= I2C_VREG_RX8025_CONTROL_REGISTER) {	// RX8025
					i2c_write_blocking(i2c0, RX8025_ADDRESS, &data, 1, false);
					if (i2c_index >= I2C_VREG_RX8025_SEC && i2c_index <= I2C_VREG_RX8025_YEAR) {
						rtc_sync_powman_timer(); // Synchronize if time changed
					}
				} else if(i2c_index >= I2C_VREG_TMP112_TEMP_MSB && i2c_index <= I2C_VREG_TMP112_THIGH_LSB) { // TMP112
					switch (i2c_index) {
						case I2C_VREG_TMP112_TEMP_MSB:
						case I2C_VREG_TMP112_TEMP_LSB:
							debug_log("Attempt to write temperature register denied.\n");
							break;
						case I2C_VREG_TMP112_CONF_MSB:
						case I2C_VREG_TMP112_TLOW_MSB:
						case I2C_VREG_TMP112_THIGH_MSB:
							i2c_write_burst_blocking(i2c0, TMP112_ADDRESS, &data, 1);
							break;
						case I2C_VREG_TMP112_CONF_LSB:
						case I2C_VREG_TMP112_TLOW_LSB:
						case I2C_VREG_TMP112_THIGH_LSB:
							i2c_write_blocking(i2c0, TMP112_ADDRESS, &data, 1, false);
							break;
						default:
							break;
					}
				}
			}
			i2c_cached_index = i2c_index;
			i2c_index = -1;
        }
        break;
    case I2C_SLAVE_REQUEST: // Master is requesting data from this slave device

        sleep_us(7);
        uint8_t data = 0x00;
        
        if (i2c_index == -1) {  // No index was written beforehand, use the cached index
            i2c_index = i2c_cached_index;
        }
        
        if (i2c_index < I2C_CONF_FIRST) {           // Read [Read-only register]
            data = get_read_only_register(i2c_index);
		} else if (i2c_index <= I2C_CONF_LAST) {    // Read [Configuration register]
		    data = get_config_register(i2c_index);
		} else if (i2c_index >= I2C_ADMIN_FIRST && i2c_index <= I2C_ADMIN_LAST) {	// Read [Admin register]
			if (i2c_index == I2C_ADMIN_DOWNLOAD) {                  // Master downloads something
		        data = download_buffer[download_buffer_index ++];
		    } else {                                                // Master reads a admin register
		        data = i2c_admin_reg[i2c_index - I2C_ADMIN_FIRST];
		    }
		    if (i2c_index == I2C_ADMIN_SHUTDOWN) {   // Master polls shutdown request / implicitly sends heartbeat
		        uint64_t ts = powman_timer_get_ms();
		        if (ts - heartbeat_update_time > 500) {
		            heartbeat_update_time = ts;
			        i2c_admin_reg[I2C_ADMIN_HEARTBEAT - I2C_ADMIN_FIRST] ++;
			        reset_heatbeat_checking_timer();
					clear_system_up_timer();
			    }
		    }
		} else if (i2c_index >= I2C_VREG_RX8025_SEC && i2c_index <= I2C_VREG_RX8025_CONTROL_REGISTER) {	// RX8025
			i2c_read_blocking(i2c0, RX8025_ADDRESS, &data, 1, false);
		} else if(i2c_index >= I2C_VREG_TMP112_TEMP_MSB && i2c_index <= I2C_VREG_TMP112_THIGH_LSB) {	// TMP112
			uint8_t buffer[2];
			switch (i2c_index) {
				case I2C_VREG_TMP112_TEMP_MSB:
					i2c_read_blocking(i2c0, TMP112_ADDRESS, buffer, 2, false);
					data = temp_msb = buffer[0];
					temp_lsb = buffer[1];
					break;
				case I2C_VREG_TMP112_TEMP_LSB:
					data = temp_lsb;
					break;
				case I2C_VREG_TMP112_CONF_MSB:
					i2c_read_blocking(i2c0, TMP112_ADDRESS, buffer, 2, false);
					data = conf_msb = buffer[0];
					conf_lsb = buffer[1];
					break;
				case I2C_VREG_TMP112_CONF_LSB:
					data = conf_lsb;
					break;
				case I2C_VREG_TMP112_TLOW_MSB:
					i2c_read_blocking(i2c0, TMP112_ADDRESS, buffer, 2, false);
					data = tlow_msb = buffer[0];
					tlow_lsb = buffer[1];
					break;
				case I2C_VREG_TMP112_TLOW_LSB:
					data = tlow_lsb;
					break;
				case I2C_VREG_TMP112_THIGH_MSB:
					i2c_read_blocking(i2c0, TMP112_ADDRESS, buffer, 2, false);
					data = thigh_msb = buffer[0];
					thigh_lsb = buffer[1];
					break;
				case I2C_VREG_TMP112_THIGH_LSB:
					data = thigh_lsb;
					break;
				default:
					break;
			}
		}
		i2c_write_byte_raw(i2c, data);
		i2c_cached_index = i2c_index;
        i2c_index = -1;
        break;
    case I2C_SLAVE_FINISH: // Master has signalled Stop / Restart
        break;
    default:
        break;
    }
}
//-----------------------------------------------------------------------------
//
// End of: Handler for slave device connected to Raspberry Pi
//
//-----------------------------------------------------------------------------


/**
 * Initialize the master device and slave device
 * Master device connects to internal I2C bus
 * Slave device connects to Raspberry Pi's I2C bus 1
 */
void i2c_devices_init(void) {

    // Initialize I2C master device
    gpio_init(I2C_MASTER_SDA_PIN);
    gpio_set_function(I2C_MASTER_SDA_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_MASTER_SDA_PIN);

    gpio_init(I2C_MASTER_SCL_PIN);
    gpio_set_function(I2C_MASTER_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_MASTER_SCL_PIN);

    i2c_init(i2c0, I2C_MASTER_BAUDRATE);

    // Initialize I2C slave device
    gpio_init(I2C_SLAVE_SDA_PIN);
    gpio_set_function(I2C_SLAVE_SDA_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SLAVE_SDA_PIN);

    gpio_init(I2C_SLAVE_SCL_PIN);
    gpio_set_function(I2C_SLAVE_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SLAVE_SCL_PIN);

    i2c_init(i2c1, I2C_SLAVE_BAUDRATE);
    i2c_slave_init(i2c1, I2C_SLAVE_ADDRESS, &i2c_slave_handler);
}


/**
 * Read data from slave device connected to internal I2C bus
 *
 * @param addr Address of the slave device
 * @param reg Index of the register
 * @param dst Pointer to buffer with data
 * @param len Length of data
 * @return Number of bytes read, or PICO_ERROR_GENERIC for error
 */
int i2c_read_from_slave(uint8_t addr, uint8_t reg, uint8_t *dst, uint32_t len) {
    int ret = i2c_write_burst_blocking(i2c0, addr, &reg, 1);
    if (ret < 0) {
        return ret;
    }
    return i2c_read_blocking(i2c0, addr, dst, len, false);
}


/**
 * Write data to slave device connected to internal I2C bus
 *
 * @param addr Address of the slave device
 * @param reg Index of the register
 * @param dst Pointer to buffer with data
 * @param len Length of data
 * @return Number of bytes written, or PICO_ERROR_GENERIC for error
 */
int i2c_write_to_slave(uint8_t addr, uint8_t reg, uint8_t *dst, uint32_t len) {
    int ret = i2c_write_burst_blocking(i2c0, addr, &reg, 1);
    if (ret < 0) {
        return ret;
    }
    return i2c_write_blocking(i2c0, addr, dst, len, false);
}


/**
 * Get value from virtual register
 *
 * @param index The index of the register
 * @return The register value, or 0 for error
 */
uint8_t get_virtual_register(uint8_t index) {
    if (index >= I2C_VREG_RX8025_SEC && index <= I2C_VREG_RX8025_CONTROL_REGISTER) { // RX8025
        uint8_t data = 0x00;
        i2c_read_from_slave(RX8025_ADDRESS, index - I2C_VREG_RX8025_SEC, &data, 1);
		return data;
	} else if(index >= I2C_VREG_TMP112_TEMP_MSB && index <= I2C_VREG_TMP112_THIGH_LSB) { // TMP112
		uint8_t data[2] = {0};
		switch (index) {
			case I2C_VREG_TMP112_TEMP_MSB:
				i2c_read_from_slave(TMP112_ADDRESS, TMP112_REG_TEMP, data, 2);
				temp_msb = data[0];
				temp_lsb = data[1];
				return temp_msb;
			case I2C_VREG_TMP112_TEMP_LSB:
				return temp_lsb;
			case I2C_VREG_TMP112_CONF_MSB:
				i2c_read_from_slave(TMP112_ADDRESS, TMP112_REG_CONF, data, 2);
				conf_msb = data[0];
				conf_lsb = data[1];
				return conf_msb;
			case I2C_VREG_TMP112_CONF_LSB:
				return conf_lsb;
			case I2C_VREG_TMP112_TLOW_MSB:
				i2c_read_from_slave(TMP112_ADDRESS, TMP112_REG_TLOW, data, 2);
				tlow_msb = data[0];
				tlow_lsb = data[1];
				return tlow_msb;
			case I2C_VREG_TMP112_TLOW_LSB:
				return tlow_lsb;
			case I2C_VREG_TMP112_THIGH_MSB:
				i2c_read_from_slave(TMP112_ADDRESS, TMP112_REG_THIGH, data, 2);
				thigh_msb = data[0];
				thigh_lsb = data[1];
				return thigh_msb;
			case I2C_VREG_TMP112_THIGH_LSB:
				return thigh_lsb;
		}
	}
	return 0x00;
}


/**
 * Set value to virtual register
 *
 * @param index The index of the register
 * @param value The value to set
 * @return 1 if success, 0 or PICO_ERROR_GENERIC otherwise
 */
int8_t set_virtual_register(uint8_t index, uint8_t value) {

    if (index >= I2C_VREG_RX8025_SEC && index <= I2C_VREG_RX8025_CONTROL_REGISTER) { // RX8025

        return i2c_write_to_slave(RX8025_ADDRESS, index - I2C_VREG_RX8025_SEC, &value, 1);

	} else if(index >= I2C_VREG_TMP112_TEMP_MSB && index <= I2C_VREG_TMP112_THIGH_LSB) { // TMP112
		uint8_t data[2] = {0};
		switch (index) {
			case I2C_VREG_TMP112_TEMP_MSB:
				temp_msb = value;
				break;
			case I2C_VREG_TMP112_TEMP_LSB:
				temp_lsb = value;
				data[0] = temp_msb;
				data[1] = temp_lsb;
				return i2c_write_to_slave(TMP112_ADDRESS, TMP112_REG_TEMP, data, 2);
			case I2C_VREG_TMP112_CONF_MSB:
				conf_msb = value;
				break;
			case I2C_VREG_TMP112_CONF_LSB:
				conf_lsb = value;
				data[0] = conf_msb;
				data[1] = conf_lsb;
				return i2c_write_to_slave(TMP112_ADDRESS, TMP112_REG_CONF, data, 2);
			case I2C_VREG_TMP112_TLOW_MSB:
				tlow_msb = value;
				break;
			case I2C_VREG_TMP112_TLOW_LSB:
				tlow_lsb = value;
				data[0] = tlow_msb;
				data[1] = tlow_lsb;
				return i2c_write_to_slave(TMP112_ADDRESS, TMP112_REG_TLOW, data, 2);
			case I2C_VREG_TMP112_THIGH_MSB:
				thigh_msb = value;
				break;
			case I2C_VREG_TMP112_THIGH_LSB:
				thigh_lsb = value;
				data[0] = thigh_msb;
				data[1] = thigh_lsb;
				return i2c_write_to_slave(TMP112_ADDRESS, TMP112_REG_THIGH, data, 2);
		}
	}
	return 0;
}


/**
 * Request Raspberry Pi to shutdown.
 * This will be performed by Witty Pi's software.
 *
 * @param shutdown true to request shutdown, false to clear the request
 */
void request_rpi_shutdown(bool shutdown) {
	i2c_admin_reg[I2C_ADMIN_SHUTDOWN - I2C_ADMIN_FIRST] = shutdown ? ADMIN_TURN_RPI_OFF : 0;
}
