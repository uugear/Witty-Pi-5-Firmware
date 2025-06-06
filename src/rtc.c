#include <stdint.h>
#include <hardware/gpio.h>
#include <hardware/powman.h>
#include <pico/time.h>
#include <string.h>

#include "rtc.h"
#include "i2c.h"
#include "log.h"
#include "main.h"
#include "conf.h"
#include "util.h"


#define SYNC_TIME_INTERVAL_US      30000000

#define TIMESTAMP_2000_01_01       946684800LL

#define RX8025_SECONDS             0x00
#define RX8025_MINUTES             0x01
#define RX8025_HOURS               0x02
#define RX8025_WEEKDAY             0x03
#define RX8025_DAY                 0x04
#define RX8025_MONTH               0x05
#define RX8025_YEAR                0x06
#define RX8025_RAM                 0x07
#define RX8025_ALARM_MINUTES       0x08
#define RX8025_ALARM_HOURS         0x09
#define RX8025_ALARM_DAYDATE       0x0A
#define RX8025_TIMER_COUNTER_0     0x0B
#define RX8025_TIMER_COUNTER_1     0x0C
#define RX8025_RTC_EXTENSION       0x0D
#define RX8025_RTC_FLAG            0x0E
#define RX8025_RTC_CONTROL         0x0F


// Alarm enable mask for date, hour and minute
#define AE_MASK         0x80


// Extension register bits
#define TSEL0           0
#define TSEL1           1
#define FSEL0           2
#define FSEL1           3
#define TE              4
#define USEL            5
#define WADA            6


// Status register bits
#define VDET            0
#define VLF             1
#define AF              3
#define TF              4
#define UF              5


// Control register bits
#define RESET           0
#define AIE             3
#define TIE             4
#define UIE             5
#define CSEL0           6
#define CSEL1           7


// Temperature compensation interval
#define	INT_0_5_SEC	    0x00
#define	INT_2_SEC       0x40
#define	INT_10_SEC	    0x80
#define	INT_30_SEC	    0xC0


static uint8_t alarm_type = ALARM_TYPE_NONE;

gpio_event_callback_t rtc_alarm_callback = NULL;

const uint8_t days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

alarm_id_t sync_timer_alarm_id = -1;


static inline bool is_pow_2(uint8_t n) {
    if (n == 0) return false;
    return (n & (n - 1)) == 0;
}


static inline int8_t get_weekday(uint8_t wbits) {
    switch(wbits) {
        case 0x01: return 0;
        case 0x02: return 1;
        case 0x04: return 2;
        case 0x08: return 3;
        case 0x10: return 4;
        case 0x20: return 5;
        case 0x40: return 6;
        default: return -1;
    }
}


/**
 * Check if given year is leap year
 * 
 * @param year The year
 * @return true if the year is a leap year, false otherwise
 */
bool is_leap_year(int year) {
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}


/**
 * Get the number of days in given month
 * 
 * @param year The year of the month
 * @param month The month (1~12)
 * @return The number of days in the month
 */
int get_days_in_month(int year, int month) {
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days_in_month[month];
}


/**
 * Convert DateTime to timestamp
 * 
 * @param dt Pointer to DateTime struct
 * @return The timestamp (total seconds since year 2000)
 */
int64_t get_total_seconds(DateTime *dt) {
    int64_t sec = 0;
    // Add years contribution
    for (int y = 2000; y < dt->year; y++) {
        sec += (is_leap_year(y) ? 366 : 365) * 86400LL;
    }
    // Add months contribution
    for (int m = 1; m < dt->month; m++) {
        sec += get_days_in_month(dt->year, m) * 86400LL;
    }
    // Add days, hours, minutes, seconds
    sec += (dt->day - 1) * 86400LL + dt->hour * 3600LL + dt->min * 60LL + dt->sec;
    return sec;
}


/**
 * Convert timestamp to DateTime
 * 
 * @param timestamp The timestamp (total seconds since year 2000)
 * @param dt Pointer to DateTime struct
 * @return true if converted successful, false otherwise
 */
void timestamp_to_datetime(int64_t timestamp, DateTime *dt) {
    int64_t seconds_remaining = timestamp;
    
    // Set initial values
    dt->year = 2000;
    dt->month = 1;
    dt->day = 1;
    dt->hour = 0;
    dt->min = 0;
    dt->sec = 0;
    dt->wday = 6;
    
    // Calculate year
    while (true) {
        int days_in_year = is_leap_year(dt->year) ? 366 : 365;
        int64_t seconds_in_year = days_in_year * 86400LL;
        
        if (seconds_remaining < seconds_in_year)
            break;
            
        seconds_remaining -= seconds_in_year;
        dt->year++;
    }
    
    // Calculate month
    while (true) {
        int days_in_current_month = get_days_in_month(dt->year, dt->month);
        int64_t seconds_in_month = days_in_current_month * 86400LL;
        
        if (seconds_remaining < seconds_in_month)
            break;
            
        seconds_remaining -= seconds_in_month;
        dt->month++;
    }
    
    // Calculate day
    dt->day = 1 + (int)(seconds_remaining / 86400LL);
    seconds_remaining %= 86400LL;
    
    // Calculate hour
    dt->hour = (int)(seconds_remaining / 3600LL);
    seconds_remaining %= 3600LL;
    
    // Calculate minute
    dt->min = (int)(seconds_remaining / 60LL);
    
    // Calculate second
    dt->sec = (int)(seconds_remaining % 60LL);
    
    // Calculate weekday using Zeller's algorithm
    int y = dt->year;
    int m = dt->month;
    int d = dt->day;
    if (m < 3) {
        m += 12;
        y--;
    }
    int k = y % 100;
    int j = y / 100;
    int h = (d + 13*(m+1)/5 + k + k/4 + j/4 + 5*j) % 7;
    dt->wday = (h + 6) % 7;    
}


// Callback when RTC alarm occurs
void rtc_alarm_occurred(void) {
    if (rtc_alarm_callback) {
        rtc_alarm_callback();
    }
    rtc_clear_alarm_flag();
}


// Callback for writting RTC time to powman timer 
int64_t sync_time_callback(alarm_id_t id, void *user_data) {
    bool rtc_time_is_valid;
	uint64_t ts = rtc_get_timestamp(&rtc_time_is_valid);
	if (rtc_time_is_valid) {
		powman_timer_set_ms((ts + TIMESTAMP_2000_01_01) * 1000);
	}
	return SYNC_TIME_INTERVAL_US;
}


// Extra processing after alarm configuration is changed
void on_alarm_conf_changed(const char *key, uint8_t old_val, uint8_t new_val) {
	if (current_rpi_state == STATE_STOPPING || current_rpi_state == STATE_OFF) {
		if (strcmp(key, CONF_ALARM1_MINUTE) == 0 || strcmp(key, CONF_ALARM1_HOUR) == 0 || strcmp(key, CONF_ALARM1_DAY) == 0) {
			int date = bcd_to_dec(conf_get(CONF_ALARM1_DAY));
			int hour = bcd_to_dec(conf_get(CONF_ALARM1_HOUR));
			int minute = bcd_to_dec(conf_get(CONF_ALARM1_MINUTE));
			int second = bcd_to_dec(conf_get(CONF_ALARM1_SECOND));
			if (date < 1 || date > 31 || hour > 23 || minute > 59 || second > 59) {
				debug_log("Clear Alarm1\n");
				rtc_set_alarm(0, 0, 0, true);
			} else {
				DateTime dt;
				rtc_get_scheduled_time(date, hour, minute, second, &dt);
				int64_t ts = get_total_seconds(&dt);
				if (adjust_action_time_for_dst(&ts)) {
					timestamp_to_datetime(ts, &dt);
				}
				debug_log("Set Alarm1 to %02d %02d:%02d\n", dt.day, dt.hour, dt.min);
				rtc_set_alarm(dt.day, dt.hour, dt.min, true);
			}
		}
	} else if (current_rpi_state == STATE_STARTING || current_rpi_state == STATE_ON) {
		if (strcmp(key, CONF_ALARM2_MINUTE) == 0 || strcmp(key, CONF_ALARM2_HOUR) == 0 || strcmp(key, CONF_ALARM2_DAY) == 0) {
			int date = bcd_to_dec(conf_get(CONF_ALARM2_DAY));
			int hour = bcd_to_dec(conf_get(CONF_ALARM2_HOUR));
			int minute = bcd_to_dec(conf_get(CONF_ALARM2_MINUTE));
			int second = bcd_to_dec(conf_get(CONF_ALARM2_SECOND));
			if (date < 1 || date > 31 || hour > 23 || minute > 59 || second > 59) {
				debug_log("Clear Alarm2\n");
				rtc_set_alarm(0, 0, 0, false);
			} else {
				DateTime dt;
				rtc_get_scheduled_time(date, hour, minute, second, &dt);
				int64_t ts = get_total_seconds(&dt);
				if (adjust_action_time_for_dst(&ts)) {
					timestamp_to_datetime(ts, &dt);
				}
				debug_log("Set Alarm2 to %02d %02d:%02d\n", dt.day, dt.hour, dt.min);
				rtc_set_alarm(dt.day, dt.hour, dt.min, false);
			}
		}
	}
}


/**
 * Initialize the RTC
 *
 * @param callback The callback function for alarm
 */ 
void rtc_init(gpio_event_callback_t callback) {

	powman_timer_start();

    uint8_t status = get_virtual_register(I2C_VREG_RX8025_FLAG_REGISTER);
    uint8_t mask = BIT_VALUE(VLF) | BIT_VALUE(VDET);
    if(status & mask) {
        set_virtual_register(I2C_VREG_RX8025_CONTROL_REGISTER, BIT_VALUE(RESET)); // Initialize a reset
    }

    set_virtual_register(I2C_VREG_RX8025_EXTENSION_REGISTER, BIT_VALUE(WADA));  // Date as alarm trigger

    set_virtual_register(I2C_VREG_RX8025_FLAG_REGISTER, 0x00);

    set_virtual_register(I2C_VREG_RX8025_CONTROL_REGISTER, (INT_2_SEC | BIT_VALUE(AIE)));   // Enable alarm
    
	rtc_sync_powman_timer();

    rtc_alarm_callback = callback;
    
    gpio_init(GPIO_RTC_INT);
    gpio_set_dir(GPIO_RTC_INT, GPIO_IN);
    gpio_pull_up(GPIO_RTC_INT);
	gpio_register_callback(GPIO_RTC_INT, GPIO_IRQ_EDGE_FALL, rtc_alarm_occurred);
	
	add_alarm_in_us(SYNC_TIME_INTERVAL_US, sync_time_callback, NULL, true);
	
	register_item_changed_callback(CONF_ALARM1_SECOND, on_alarm_conf_changed);
	register_item_changed_callback(CONF_ALARM1_MINUTE, on_alarm_conf_changed);
	register_item_changed_callback(CONF_ALARM1_HOUR, on_alarm_conf_changed);
	register_item_changed_callback(CONF_ALARM1_DAY, on_alarm_conf_changed);
	register_item_changed_callback(CONF_ALARM2_SECOND, on_alarm_conf_changed);
	register_item_changed_callback(CONF_ALARM2_MINUTE, on_alarm_conf_changed);
	register_item_changed_callback(CONF_ALARM2_HOUR, on_alarm_conf_changed);
	register_item_changed_callback(CONF_ALARM2_DAY, on_alarm_conf_changed);
}


/**
 * Get RTC time
 *
 * @param time_info Pointer to tm structure who stores the time information
 * @return true if the retrieved time is valid, false otherwise
 */
bool rtc_get_time(DateTime *dt) {
    if (!dt) {
        return false;
    }
    uint8_t reg[7];
    i2c_read_from_slave(RX8025_ADDRESS, RX8025_SECONDS, reg, 7);
    
    reg[RX8025_SECONDS] = bcd_to_dec(reg[RX8025_SECONDS]);
    reg[RX8025_MINUTES] = bcd_to_dec(reg[RX8025_MINUTES]);
    reg[RX8025_HOURS] = bcd_to_dec(reg[RX8025_HOURS]);
    reg[RX8025_DAY] = bcd_to_dec(reg[RX8025_DAY]);
    reg[RX8025_MONTH] = bcd_to_dec(reg[RX8025_MONTH]);
    reg[RX8025_YEAR] = bcd_to_dec(reg[RX8025_YEAR]);
    
    bool valid = (
        reg[RX8025_SECONDS] < 60
     && reg[RX8025_MINUTES] < 60
     && reg[RX8025_HOURS] < 24
     && is_pow_2(reg[RX8025_WEEKDAY]) && reg[RX8025_WEEKDAY] <= 0x40
     && reg[RX8025_DAY] > 0 && reg[RX8025_DAY] <= 31
     && reg[RX8025_MONTH] > 0 && reg[RX8025_MONTH] <= 12
     && reg[RX8025_YEAR] < 100
    );
    
    dt->year = reg[RX8025_YEAR] + 2000;
    dt->month = reg[RX8025_MONTH];
    dt->day = reg[RX8025_DAY];
    dt->hour = reg[RX8025_HOURS];
    dt->min = reg[RX8025_MINUTES];
    dt->sec = reg[RX8025_SECONDS];
    dt->wday = get_weekday(reg[RX8025_WEEKDAY]);
    
    return valid;
}


/**
 * Get RTC timestamp
 *
 * @param time_info Pointer to tm structure who stores the time information
 * @return the total seconds since year 2000
 */
int64_t rtc_get_timestamp(bool *valid) {
    DateTime dt = {0};
    bool is_valid = rtc_get_time(&dt);
    if (valid) {
        *valid = is_valid;
    }
    return get_total_seconds(&dt);
}


/**
 * Set RTC time
 *
 * @param dt Pointer to DateTime object
 * @return true if set successfully, otherwise false
 */
bool rtc_set_time(DateTime *dt) {
    if (!dt) {
        return false;
    }

    uint8_t reg[7];
    reg[RX8025_YEAR] = dec_to_bcd(dt->year - 2000);
    reg[RX8025_MONTH] = dec_to_bcd(dt->month);
    reg[RX8025_DAY] = dec_to_bcd(dt->day);
    reg[RX8025_HOURS] = dec_to_bcd(dt->hour);
    reg[RX8025_MINUTES] = dec_to_bcd(dt->min);
    reg[RX8025_SECONDS] = dec_to_bcd(dt->sec);
    reg[RX8025_WEEKDAY] = BIT_VALUE(dec_to_bcd(dt->wday));

    if (i2c_write_to_slave(RX8025_ADDRESS, RX8025_SECONDS, reg, 7)) {
		rtc_sync_powman_timer();
		return true;
	}
    return false;
}


/**
 * Set RTC timestamp
 *
 * @param timestamp The total seconds since year 2000
 * @return true if set successfully, otherwise false
 */
bool rtc_set_timestamp(int64_t timestamp) {
	DateTime dt;
	timestamp_to_datetime(timestamp, &dt);
	return rtc_set_time(&dt);
}


// Callback for synchronizing the powman timer with RTC
int64_t sync_powman_timer_callback(alarm_id_t id, void *user_data) {
    bool rtc_time_is_valid;
	uint64_t ts = rtc_get_timestamp(&rtc_time_is_valid);
	if (rtc_time_is_valid) {
	    debug_log("Write RTC time to POWMAN timer.\n");
		powman_timer_set_ms((ts + TIMESTAMP_2000_01_01) * 1000);
	} else {
	    debug_log("Write POWMAN time to RTC.\n");
		rtc_set_timestamp(powman_timer_get_ms() / 1000 - TIMESTAMP_2000_01_01);
	}
    return 0;
}


/**
 * Synchronize the powman timer with RTC
 */
void rtc_sync_powman_timer(void) {
    cancel_alarm(sync_timer_alarm_id);
	sync_timer_alarm_id = add_alarm_in_us(100000, sync_powman_timer_callback, NULL, true);
}


/**
 * Clear RTC alarm flag (so alarm can occur again)
 * This function also clears the type of current scheduled alarm
 */
void rtc_clear_alarm_flag(void) {
    uint8_t status = get_virtual_register(I2C_VREG_RX8025_FLAG_REGISTER);
    set_virtual_register(I2C_VREG_RX8025_FLAG_REGISTER, (status & (~BIT_VALUE(AF))));
    alarm_type = ALARM_TYPE_NONE;
}



/**
 * Set RTC alarm
 *
 * @param day The day of alarm, use negative value to skip
 * @param hour The hour of alarm, use negative value to skip
 * @param min The minute of alarm, use negative value to skip
 * @param startup Whether the alarm is for startup
 * @return true if set successfully, false otherwise
 */
void rtc_set_alarm(int8_t day, int8_t hour, int8_t min, bool startup) {
    set_virtual_register(I2C_VREG_RX8025_DAY_ALARM, day >= 0 ? dec_to_bcd(day) : AE_MASK);
    set_virtual_register(I2C_VREG_RX8025_HOUR_ALARM, hour >= 0 ? dec_to_bcd(hour) : AE_MASK);
    set_virtual_register(I2C_VREG_RX8025_MIN_ALARM, min >= 0 ? dec_to_bcd(min) : AE_MASK);
    if (day != 0 || hour != 0 || min != 0) {
		alarm_type = startup ? ALARM_TYPE_STARTUP : ALARM_TYPE_SHUTDOWN;
	} else {
		if ((alarm_type == ALARM_TYPE_STARTUP && startup) || (alarm_type == ALARM_TYPE_SHUTDOWN && !startup)) {
			alarm_type = ALARM_TYPE_NONE;
		}
	}
}


/**
 * Get actual scheduled time by day, hour and minute values
 *
 * @param day The scheduled day
 * @param hour The scheduled hour
 * @param min The scheduled minute
 * @param sec The scheduled second
 * @param dt Pointer to DateTime object that stores the time
 */
void rtc_get_scheduled_time(int8_t day, int8_t hour, int8_t min, int8_t sec, DateTime *dt) {
    if (dt) {
        day &= 0x7F;
        hour &= 0x7F;
        min &= 0x7F;
        sec &= 0x7F;
        
        rtc_get_time(dt);
        
        if (dt->day < day) {
            dt->month++;
            if (dt->month > 12) {
                dt->month = 1;
                dt->year++;
            }
        }
        dt->day = day;
        dt->hour = hour;
        dt->min = min;
        dt->sec = sec;
    }
}


/**
 * Get the type of current scheduled alarm
 *
 * @return The type of current alarm
 */
uint8_t rtc_get_alarm_type(void) {
    return alarm_type;
}


/**
 * Check whether Raspberry Pi can be woke up at current time
 *
 * @return true or false
 */
//bool can_cur_time_turn_on_rpi(void) {
//	return alarm_type != ALARM_TYPE_STARTUP;
//}


/**
 * Check whether Raspberry Pi can be shut down at current time
 *
 * @return true or false
 */
bool can_cur_time_turn_off_rpi(void) {
	if (current_rpi_state == STATE_OFF || current_rpi_state == STATE_STOPPING) {
        bool valid;
    	int64_t cur_ts = rtc_get_timestamp(&valid);
    	if (valid) {
    		uint8_t sec = bcd_to_dec(conf_get(CONF_ALARM1_SECOND));
    		uint8_t min = bcd_to_dec(conf_get(CONF_ALARM1_MINUTE));
    		uint8_t hour = bcd_to_dec(conf_get(CONF_ALARM1_HOUR));
    		uint8_t day = bcd_to_dec(conf_get(CONF_ALARM1_DAY));
    		DateTime dt;
    		rtc_get_scheduled_time(day, hour, min, sec, &dt);
    		int64_t scheduled_ts = get_total_seconds(&dt);
    		if (cur_ts < scheduled_ts) {
    		    return true;
    		}
    	}
    }
    return false;
}


/**
 * Convert the specific weekday in the x week in y month to DateTime
 * 
 * @param year Year (2000~2099)
 * @param month Month (1~12)
 * @param week Week (1~5, 0 means the last week)
 * @param wday Weekday (0~6, 0=Sunday)
 * @param hour Hour (0~23)
 * @param min Minute (0~59)
 * @param dt Pointer to DateTime object
 * @return true for succesful convertion, otherwise false
 */
bool convert_date(int16_t year, uint8_t month, uint8_t week, uint8_t wday, uint8_t hour, uint8_t min, DateTime *dt) {
    if (!dt || month < 1 || month > 12 || week > 5 || year < 2000 || year > 2099 || hour > 23 || min > 59) {
        return false;
    }
    
    // Calculate the weekday for the first day in the month
    // Use Zeller formula: h = (q + 13*(m+1)/5 + K + K/4 + J/4 + 5*J) % 7
    int m = month;
    int y = year;
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int K = y % 100;
    int J = y / 100;
    int h = (1 + 13*(m+1)/5 + K + K/4 + J/4 + 5*J) % 7;
    int first_day_of_month = (h + 6) % 7;
    
    int target_day = 1 + (wday + 7 - first_day_of_month) % 7;
    
    int days = get_days_in_month(year, month);
    
    if (week == 0) {    // For the last week (week=0)
        int last_day = days;
        int last_day_wday = (first_day_of_month + days - 1) % 7;
        target_day = last_day - ((last_day_wday - wday + 7) % 7);
    } else {            // For non-last week
        target_day += (week - 1) * 7;
        if (target_day > days) {
            return false;
        }
    }
    
    dt->year = year;
    dt->month = month;
    dt->day = target_day;
    dt->hour = hour;
    dt->min = min;
    dt->sec = 0;
    dt->wday = wday;
    
    return true;
}


/**
 * Load DST configuration
 *
 * @param offset Pointer to DST offset (in minute), 0 if DST is disabled
 * @param begin_ts Pointer to DST begin timestamp
 * @param end_ts Pointer to DST end timestamp
 * @param north_hemisphere Pointer to boolean variable, true for north hemisphere
 *
 * @return -1 if something goes wrong, otherwise return the current timestamp
 */
int64_t load_dst_configuration(uint8_t * p_offset, int64_t * p_begin_ts, int64_t * p_end_ts, bool * p_north_hemisphere) {
	if (!p_offset || !p_begin_ts || !p_end_ts || !p_north_hemisphere) {
		return -1;
	}
	DateTime cur_dt;
    bool valid = rtc_get_time(&cur_dt);
    if (valid) {
        int64_t cur_ts = get_total_seconds(&cur_dt);
        
        *p_offset = conf_get(CONF_DST_OFFSET);
        uint8_t mode = ((*p_offset & 0x80) >> 7);
        *p_offset &= 0x7F;
        
        uint8_t begin_month = bcd_to_dec(conf_get(CONF_DST_BEGIN_MON));
        uint8_t begin_day = bcd_to_dec(conf_get(CONF_DST_BEGIN_DAY));
        uint8_t begin_hour = bcd_to_dec(conf_get(CONF_DST_BEGIN_HOUR));
        uint8_t begin_min = bcd_to_dec(conf_get(CONF_DST_BEGIN_MIN));
        
        uint8_t end_month = bcd_to_dec(conf_get(CONF_DST_END_MON));
        uint8_t end_day = bcd_to_dec(conf_get(CONF_DST_END_DAY));
        uint8_t end_hour = bcd_to_dec(conf_get(CONF_DST_END_HOUR));
        uint8_t end_min = bcd_to_dec(conf_get(CONF_DST_END_MIN));
        
        DateTime begin_dt, end_dt;
        if (mode == 0) { // Mode 0: specify a weekday in the nth week of the month
            uint8_t begin_week = begin_day / 10;
            uint8_t begin_wday = begin_day % 10;
            if (!convert_date(cur_dt.year, begin_month, begin_week, begin_wday, begin_hour, begin_min, &begin_dt)) {
                return -1;
            }
            uint8_t end_week = end_day / 10;
            uint8_t end_wday = end_day % 10;
            if (!convert_date(cur_dt.year, end_month, end_week, end_wday, end_hour, end_min, &end_dt)) {
                return -1;
            }
        } else {	// Mode 1: specify a date in the month
            begin_dt.year = cur_dt.year;
            begin_dt.month = begin_month;
            begin_dt.day = begin_day;
            begin_dt.hour = begin_hour;
            begin_dt.min = begin_min;
            begin_dt.sec = 0;
            
            end_dt.year = cur_dt.year;
            end_dt.month = end_month;
            end_dt.day = end_day;
            end_dt.hour = end_hour;
            end_dt.min = end_min;
            end_dt.sec = 0;
        }
        *p_begin_ts = get_total_seconds(&begin_dt);
        *p_end_ts = get_total_seconds(&end_dt);
		
		*p_north_hemisphere = (end_month > begin_month);
		
		return cur_ts;
	}
	return -1;
}


/**
 * Apply daylight-saving time (DST) if needed
 *
 * @return true if newly applied, otherwise false
 */
bool apply_dst_if_needed(void) {
	uint8_t offset;
	int64_t begin_ts, end_ts;
	bool north_hemisphere;
	int64_t cur_ts = load_dst_configuration(&offset, &begin_ts, &end_ts, &north_hemisphere);
	if (cur_ts != -1 && offset != 0) {
		bool dst_applied = (conf_get(CONF_DST_APPLIED) > 0);
        bool dst_required = false;
        if (north_hemisphere) {
            dst_required = (cur_ts >= begin_ts && cur_ts < end_ts);
        } else {
            dst_required = (cur_ts < end_ts || cur_ts >= begin_ts);
        }
        if (dst_required) {
            if (!dst_applied) {
                // Begin DST: set clocks forward
				rtc_set_timestamp(cur_ts + offset);
				conf_set(CONF_DST_APPLIED, 1);
				conf_sync();
                return true;
            }
        } else {
            if (dst_applied) {
                // End DST: set clocks backward
				rtc_set_timestamp(cur_ts - offset);
				conf_set(CONF_DST_APPLIED, 0);
				conf_sync();
                return true;
            }
        }
    }
    return false;
}


/**
 * Adjust given startup/shutdown timestamp for incoming DST switching
 *
 * @param p_action_ts The pointer to action timestamp
 * @return true if adjustment has been made, otherwise false
 */
bool adjust_action_time_for_dst(uint64_t * p_action_ts) {
	if (!p_action_ts) {
		return false;
	}
	uint8_t offset;
	int64_t begin_ts, end_ts;
	bool north_hemisphere;
	int64_t cur_ts = load_dst_configuration(&offset, &begin_ts, &end_ts, &north_hemisphere);
	if (cur_ts != -1 && offset != 0 && cur_ts < *p_action_ts) {
		bool dst_applied = (conf_get(CONF_DST_APPLIED) > 0);
        bool dst_required = false;
        if (north_hemisphere) {
            dst_required = (*p_action_ts >= begin_ts && *p_action_ts < end_ts);
        } else {
            dst_required = (*p_action_ts < end_ts || *p_action_ts >= begin_ts);
        }
		if (dst_required) {
			if (!dst_applied) {
				*p_action_ts += offset;
				return true;
			}
		} else {
			if (dst_applied) {
				*p_action_ts -= offset;
				return true;
			}
		}
	}
	return false;
}


/**
 * Load scheduled shutdown from configuration and set it to RTC alarm
 *
 * @param startup Whether it is for startup (false for shutdown)
 */
void load_and_schedule_alarm(bool startup) {
	bool valid;
	int64_t cur_ts = rtc_get_timestamp(&valid);
	if (valid) {
		uint8_t sec = bcd_to_dec(conf_get(startup ? CONF_ALARM1_SECOND : CONF_ALARM2_SECOND));
		uint8_t min = bcd_to_dec(conf_get(startup ? CONF_ALARM1_MINUTE : CONF_ALARM2_MINUTE));
		uint8_t hour = bcd_to_dec(conf_get(startup ? CONF_ALARM1_HOUR : CONF_ALARM2_HOUR));
		uint8_t day = bcd_to_dec(conf_get(startup ? CONF_ALARM1_DAY : CONF_ALARM2_DAY));
		DateTime dt;
		rtc_get_scheduled_time(day, hour, min, sec, &dt);
		int64_t scheduled_ts = get_total_seconds(&dt);
		if (cur_ts < scheduled_ts) {
			if (adjust_action_time_for_dst(&scheduled_ts)) {
				timestamp_to_datetime(scheduled_ts, &dt);
				rtc_set_alarm(dt.day, dt.hour, dt.min, startup);
			} else {
				rtc_set_alarm(day, hour, min, startup);
				debug_log("Set Alarm %02d %02d:%02d for %s\n", day, hour, min, startup ? "startup" : "shutdown");
			}
		}
	}
}
