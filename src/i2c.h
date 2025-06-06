#ifndef _I2C_H_
#define _I2C_H_

#include <stdint.h>


#define RX8025_ADDRESS				0x32
#define TMP112_ADDRESS				0x48


/*
 * read-only registers
 */
#define I2C_FW_ID                   0   // [0x00] Firmware id
#define I2C_FW_VERSION_MAJOR		1   // [0x01] Firmware major version
#define I2C_FW_VERSION_MINOR		2	// [0x02] Firmware minor version (x100)
 
#define I2C_VUSB_MV_MSB				3	// [0x03] Most significant byte of Vusb (unit: mV)
#define I2C_VUSB_MV_LSB				4	// [0x04] Least significant byte of Vusb (unit: mV)
#define I2C_VIN_MV_MSB				5	// [0x05] Most significant byte of Vin (unit: mV)
#define I2C_VIN_MV_LSB				6	// [0x06] Least significant byte of Vin (unit: mV)
#define I2C_VOUT_MV_MSB				7	// [0x07] Most significant byte of Vout (unit: mV)
#define I2C_VOUT_MV_LSB				8	// [0x08] Least significant byte of Vout (unit: mV)
#define I2C_IOUT_MA_MSB				9	// [0x09] Most significant byte of Iout (unit: mA)
#define I2C_IOUT_MA_LSB				10	// [0x0A] Least significant byte of Iout (unit: mA)
 
#define I2C_POWER_MODE			    11  // [0x0B] Power mode: 0=via Vusb, 1=via Vin
 
#define I2C_MISSED_HEARTBEAT        12  // [0x0C] Missed heartbeat count
 
#define I2C_RPI_STATE               13  // [0x0D] Current Raspberry Pi state: 0=OFF, 1=STARTING, 2=ON, 3=STOPPING
 
#define I2C_ACTION_REASON           14  // [0x0E] The latest action reason (see ACTION_REASON_???). Higher 4 bits for startup and lower 4 bits for shutdown.
 
#define I2C_MISC					15	// [0x0F] Miscellaneous state: b0-schedule script in use
 
 
/*
 * readable/writable registers
 */
#define I2C_CONF_FIRST              16  // ------
 
#define I2C_CONF_ADDRESS            16  // [0x10] I2C slave address: defaul=0x51
 
#define I2C_CONF_DEFAULT_ON_DELAY   17  // [0x11] The delay (in second) between power connection and turning on Pi: default=255(off)
#define I2C_CONF_POWER_CUT_DELAY    18  // [0x12] The delay (in second) between Pi shutdown and power cut: default=15
 
#define I2C_CONF_PULSE_INTERVAL     19  // [0x13] Pulse interval in seconds, for LED and dummy load: default=5
#define I2C_CONF_BLINK_LED          20  // [0x14] How long the white LED should stay on (in ms), 0 if white LED should not blink.
#define I2C_CONF_DUMMY_LOAD         21  // [0x15] How long the dummy load should be applied (in ms), 0 if dummy load is off.
 
#define I2C_CONF_LOW_VOLTAGE        22  // [0x16] Low voltage threshold (x10), 0=disabled
#define I2C_CONF_RECOVERY_VOLTAGE   23  // [0x17] Voltage (x10) that triggers recovery, 0=disabled
 
#define I2C_CONF_PS_PRIORITY        24  // [0x18] Power source priority, 0=Vusb first, 1=Vin first
 
#define I2C_CONF_ADJ_VUSB           25  // [0x19] Adjustment for measured Vusb (x100), range from -127 to 127
#define I2C_CONF_ADJ_VIN            26  // [0x1A] Adjustment for measured Vin (x100), range from -127 to 127
#define I2C_CONF_ADJ_VOUT           27  // [0x1B] Adjustment for measured Vout (x100), range from -127 to 127
#define I2C_CONF_ADJ_IOUT           28  // [0x1C] Adjustment for measured Iout (x1000), range from -127 to 127
 
#define I2C_CONF_WATCHDOG           29  // [0x1D] Allowed missing heartbeats before power cycle by watchdog, default=0(disable watchdog)
 
#define I2C_CONF_LOG_TO_FILE		30	// [0x1E] Whether to write log into file: 1=allowed, 0=not allowed
 
#define I2C_CONF_BOOTSEL_FTY_RST	31	// [0x1F] Whether to allow long press BOOTSEL and then click button for factory reset: 1=allowed, 0=not allowed
 
#define I2C_CONF_ALARM1_SECOND      32  // [0x20] Second register for startup alarm (BCD format)
#define I2C_CONF_ALARM1_MINUTE      33  // [0x21] Minute register for startup alarm (BCD format)
#define I2C_CONF_ALARM1_HOUR        34  // [0x22] Hour register for startup alarm (BCD format)
#define I2C_CONF_ALARM1_DAY         35  // [0x23] Day register for startup alarm (BCD format)
 
#define I2C_CONF_ALARM2_SECOND      36  // [0x24] Second register for shutdown alarm (BCD format)
#define I2C_CONF_ALARM2_MINUTE      37  // [0x25] Minute register for shutdown alarm (BCD format)
#define I2C_CONF_ALARM2_HOUR        38  // [0x26] Hour register for shutdown alarm (BCD format)
#define I2C_CONF_ALARM2_DAY         39  // [0x27] Day register for shutdown alarm (BCD format)
 
#define I2C_CONF_BELOW_TEMP_ACTION  40  // [0x28] Action for below temperature: 0-do nothing; 1-startup; 2-shutdown
#define I2C_CONF_BELOW_TEMP_POINT   41  // [0x29] Set point for below temperature (signed degrees of Celsius)
#define I2C_CONF_OVER_TEMP_ACTION   42  // [0x2A] Action for over temperature: 0-do nothing; 1-startup; 2-shutdown
#define I2C_CONF_OVER_TEMP_POINT    43  // [0x2B] Set point for over temperature (signed degrees of Celsius)
 
#define I2C_CONF_DST_OFFSET         44  // [0x2C] b7=mode; b6~b0: DST offset in minute, default=0(disable DST)
#define I2C_CONF_DST_BEGIN_MON      45  // [0x2D] DST begin month in BCD format
#define I2C_CONF_DST_BEGIN_DAY      46  // [0x2E] mode=0: b7~b4=week in BCD, b3~b0=day in BCD; mode=1: b7~b0=date in BCD
#define I2C_CONF_DST_BEGIN_HOUR     47  // [0x2F] DST begin hour in BCD format
#define I2C_CONF_DST_BEGIN_MIN      48  // [0x30] DST begin minute in BCD format
#define I2C_CONF_DST_END_MON        49  // [0x31] DST end month in BCD format
#define I2C_CONF_DST_END_DAY        50  // [0x32] mode=0: b7~b4=week in BCD, b3~b0=day in BCD; mode=1: b7~b0=date in BCD
#define I2C_CONF_DST_END_HOUR       51  // [0x33] DST end hour in BCD format
#define I2C_CONF_DST_END_MIN        52  // [0x34] DST end minute in BCD format
#define I2C_CONF_DST_APPLIED        53  // [0x35] Whether DST has been applied
 
#define I2C_CONF_SYS_CLOCK_MHZ		54	// [0x36] System clock (in MHz) for RP2350: default=48 (required for USB drive and USB-uart)
 
#define I2C_CONF_LAST               63  // ------
#define I2C_ADMIN_FIRST             64  // ------
 
#define I2C_ADMIN_DIR  		        64  // [0x40] Register to specify directory
#define I2C_ADMIN_CONTEXT  		    65  // [0x41] Register to provide extra context
#define I2C_ADMIN_DOWNLOAD		    66  // [0x42] Register to provide download stream
#define I2C_ADMIN_UPLOAD		    67  // [0x43] Register to provide upload stream
 
#define I2C_ADMIN_PASSWORD		    68  // [0x44] Password for administrative command, will be cleared automatically after running the command
#define I2C_ADMIN_COMMAND		    69  // [0x45] Administrative command to run, will be cleared automatically after running the command
 
#define I2C_ADMIN_HEARTBEAT         70  // [0x46] Heartbeat register for watchdog
 
#define I2C_ADMIN_SHUTDOWN          71  // [0x47] Register for shutdown request

#define I2C_ADMIN_LAST              79  // ------


/*
 * virtual registers (mapped to RX8025 or TMP112)
 */
#define I2C_VREG_FIRST                      80  // ------

#define I2C_VREG_RX8025_SEC					80  // [0x50] Second in RTC time (0~59)
#define I2C_VREG_RX8025_MIN					81  // [0x51] Minute in RTC time (0~59)
#define I2C_VREG_RX8025_HOUR				82  // [0x52] Hour in RTC time (0~23)
#define I2C_VREG_RX8025_WEEKDAY				83  // [0x53] Weekday in RTC time (0~6)
#define I2C_VREG_RX8025_DAY					84  // [0x54] Date in RTC time (1~28/29/30/31)
#define I2C_VREG_RX8025_MONTH				85  // [0x55] Month in RTC time (1~12)
#define I2C_VREG_RX8025_YEAR				86  // [0x56] Year in RTC time (0~99)
#define I2C_VREG_RX8025_RAM					87  // [0x57] RTC RAM register
#define I2C_VREG_RX8025_MIN_ALARM			88  // [0x58] RTC minute alarm register
#define I2C_VREG_RX8025_HOUR_ALARM			89  // [0x59] RTC hour alarm register
#define I2C_VREG_RX8025_DAY_ALARM			90  // [0x5A] RTC day alarm register
#define I2C_VREG_RX8025_TIMER_COUNTER0		91  // [0x5B] RTC timer Counter 0
#define I2C_VREG_RX8025_TIMER_COUNTER1		92  // [0x5C] RTC timer Counter 1
#define I2C_VREG_RX8025_EXTENSION_REGISTER	93  // [0x5D] RTC extension register
#define I2C_VREG_RX8025_FLAG_REGISTER		94  // [0x5E] RTC flag register
#define I2C_VREG_RX8025_CONTROL_REGISTER	95  // [0x5F] RTC control register

#define I2C_VREG_TMP112_TEMP_MSB			96  // [0x60] MSB of temperature
#define I2C_VREG_TMP112_TEMP_LSB			97  // [0x61] LSB of temperature
#define I2C_VREG_TMP112_CONF_MSB			98  // [0x62] MSB of configuration
#define I2C_VREG_TMP112_CONF_LSB			99  // [0x63] LSB of configuration
#define I2C_VREG_TMP112_TLOW_MSB			100 // [0x64] MSB of low-temperature threshold
#define I2C_VREG_TMP112_TLOW_LSB			101 // [0x65] LSB of low-temperature threshold
#define I2C_VREG_TMP112_THIGH_MSB			102 // [0x66] MSB of high-temperature threshold
#define I2C_VREG_TMP112_THIGH_LSB			103 // [0x67] LSB of high-temperature threshold

#define I2C_VREG_LAST                       103 // ------


/*
 * I2C administrative command form: 2 bytes (password + command)
 * When writing it via I2C, make sure to write password byte first
 */
#define I2C_ADMIN_PWD_CMD_PRINT_PRODUCT_INFO        0x17F0
#define I2C_ADMIN_PWD_CMD_FORMAT_DISK               0x37FD
#define I2C_ADMIN_PWD_CMD_RESET_RTC                 0x387C
#define I2C_ADMIN_PWD_CMD_ENABLE_ID_EEPROM_WP       0x81EE
#define I2C_ADMIN_PWD_CMD_DISABLE_ID_EEPROM_WP      0x82ED
#define I2C_ADMIN_PWD_CMD_RESET_CONF                0x945B
#define I2C_ADMIN_PWD_CMD_SYNC_CONF                 0x955C
#define I2C_ADMIN_PWD_CMD_SAVE_LOG                  0x975D
#define I2C_ADMIN_PWD_CMD_LOAD_SCRIPT               0x9915
#define I2C_ADMIN_PWD_CMD_LIST_FILES                0xA0F1
#define I2C_ADMIN_PWD_CMD_CHOOSE_SCRIPT             0xA159
#define I2C_ADMIN_PWD_CMD_PURGE_SCRIPT              0xA260


/*
 * Reason for latest action (used by I2C_ACTION_REASON register)
 */
#define ACTION_REASON_UNKNOWN               0
#define ACTION_REASON_ALARM1                1
#define ACTION_REASON_ALARM2                2
#define ACTION_REASON_BUTTON_CLICK          3
#define ACTION_REASON_VIN_DROP              4
#define ACTION_REASON_VIN_RECOVER           5
#define ACTION_REASON_OVER_TEMPERATURE      6
#define ACTION_REASON_BELOW_TEMPERATURE     7
#define ACTION_REASON_POWER_CONNECTED       8
#define ACTION_REASON_REBOOT                9
#define ACTION_REASON_MISSED_HEARTBEAT      10
#define ACTION_REASON_EXTERNAL_SHUTDOWN     11
#define ACTION_REASON_EXTERNAL_REBOOT       12


/*
 * I2C file access
 */
#define DIRECTORY_NONE			0
#define DIRECTORY_ROOT			1
#define DIRECTORY_CONF			2
#define DIRECTORY_LOG			3
#define DIRECTORY_SCHEDULE		4

#define PACKET_BEGIN            '<'
#define PACKET_DELIMITER        '|'
#define PACKET_END              '>'
 

/**
 * Initialize the master device and slave device
 * Master device connects to internal I2C bus
 * Slave device connects to Raspberry Pi's I2C bus 1
 */
void i2c_devices_init(void);


/**
 * Read data from slave device connected to internal I2C bus
 * 
 * @param addr Address of the slave device
 * @param reg Index of the register
 * @param dst Pointer to buffer with data
 * @param len Length of data
 * @return Number of bytes read, or PICO_ERROR_GENERIC for error
 */
int i2c_read_from_slave(uint8_t addr, uint8_t reg, uint8_t *dst, uint32_t len);


/**
 * Write data to slave device connected to internal I2C bus
 * 
 * @param addr Address of the slave device
 * @param reg Index of the register
 * @param dst Pointer to buffer with data
 * @param len Length of data
 * @return Number of bytes written, or PICO_ERROR_GENERIC for error
 */
int i2c_write_to_slave(uint8_t addr, uint8_t reg, uint8_t *dst, uint32_t len);


/**
 * Get value from read-only register
 * 
 * @param index The index of the register
 * @return The register value, or 0 for error
 */
uint8_t get_read_only_register(uint8_t index);


/**
 * Get value from configuration register
 * 
 * @param index The index of the register
 * @return The value of the register
 */
uint8_t get_config_register(uint8_t index);


/**
 * Set value to configuration register
 * 
 * @param index The index of the register
 * @param value The value to set
 */
void set_config_register(uint8_t index, uint8_t value);

/**
 * Get value from virtual register
 * 
 * @param index The index of the register
 * @return The register value, or 0 for error
 */
uint8_t get_virtual_register(uint8_t index);


/**
 * Set value to virtual register
 * 
 * @param index The index of the register
 * @param value The value to set
 * @return 1 if success, 0 or PICO_ERROR_GENERIC otherwise
 */
int8_t set_virtual_register(uint8_t index, uint8_t value);


/**
 * Run administrative command
 */
void run_admin_command();


/**
 * Request Raspberry Pi to shutdown.
 * This will be performed by Witty Pi's software.
 *
 * @param shutdown true to request shutdown, false to clear the request
 */
void request_rpi_shutdown(bool shutdown);


#endif