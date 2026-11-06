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
#include "script.h"
#include "dst.h"


#define SYNC_TIME_INTERVAL_US      30000000

#define RTC_ALARM_CONF_APPLY_DELAY_MS   100u

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
static volatile bool rtc_gpio_alarm_seen = false;

gpio_event_callback_t rtc_alarm_callback = NULL;

const uint8_t days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

alarm_id_t sync_timer_alarm_id = -1;

static bool alarm1_conf_changed_pending = false;
static bool alarm2_conf_changed_pending = false;
static absolute_time_t alarm_conf_apply_after;
static bool alarm_conf_apply_scheduled = false;

static bool rtc_set_alarm_interrupt_enabled(bool enabled);
static bool rtc_clear_alarm_flag_checked(void);
static bool rtc_update_alarm(int8_t day, int8_t hour, int8_t min, bool startup);

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
    /* Defer all I2C operations and action dispatch out of the GPIO IRQ. */
    rtc_gpio_alarm_seen = true;
}

bool rtc_take_gpio_alarm(void) {
    bool seen = rtc_gpio_alarm_seen;
    rtc_gpio_alarm_seen = false;
    return seen;
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


static void rtc_apply_alarm1_conf(void) {
    int date = bcd_to_dec(conf_get(CONF_ALARM1_DAY));
	int hour = bcd_to_dec(conf_get(CONF_ALARM1_HOUR));
	int minute = bcd_to_dec(conf_get(CONF_ALARM1_MINUTE));
	int second = bcd_to_dec(conf_get(CONF_ALARM1_SECOND));
	if (date < 1 || date > 31 || hour > 23 || minute > 59 || second > 59) {
		debug_log("Clear Alarm1\n");
		rtc_set_alarm(0, 0, 0, true);
	} else {
		DateTime dt;
        if (!rtc_get_scheduled_time(date, hour, minute, second, &dt)) {
            debug_log("Cannot resolve next configured RTC alarm date.\n");
            return;
        }
        int64_t ts = dst_action_to_hardware_local(get_total_seconds(&dt));
        timestamp_to_datetime(ts, &dt);
		debug_log("Set Alarm1 to %02d %02d:%02d\n", dt.day, dt.hour, dt.min);
		rtc_set_alarm(dt.day, dt.hour, dt.min, true);
	}
}

static void rtc_apply_alarm2_conf(void) {
    int date = bcd_to_dec(conf_get(CONF_ALARM2_DAY));
	int hour = bcd_to_dec(conf_get(CONF_ALARM2_HOUR));
	int minute = bcd_to_dec(conf_get(CONF_ALARM2_MINUTE));
	int second = bcd_to_dec(conf_get(CONF_ALARM2_SECOND));
	if (date < 1 || date > 31 || hour > 23 || minute > 59 || second > 59) {
		debug_log("Clear Alarm2\n");
		rtc_set_alarm(0, 0, 0, false);
	} else {
		DateTime dt;
        if (!rtc_get_scheduled_time(date, hour, minute, second, &dt)) {
            debug_log("Cannot resolve next configured RTC alarm date.\n");
            return;
        }
        int64_t ts = dst_action_to_hardware_local(get_total_seconds(&dt));
        timestamp_to_datetime(ts, &dt);
		debug_log("Set Alarm2 to %02d %02d:%02d\n", dt.day, dt.hour, dt.min);
		rtc_set_alarm(dt.day, dt.hour, dt.min, false);
	}
}

// Extra processing after alarm configuration is changed
void on_alarm_conf_changed(const char *key, uint8_t old_val, uint8_t new_val) {
	if (current_rpi_state == STATE_STOPPING || current_rpi_state == STATE_OFF) {
		if (strcmp(key, CONF_ALARM1_MINUTE) == 0 || strcmp(key, CONF_ALARM1_HOUR) == 0 || strcmp(key, CONF_ALARM1_DAY) == 0) {
		   alarm1_conf_changed_pending = true;
		}
	} else if (current_rpi_state == STATE_STARTING || current_rpi_state == STATE_ON) {
		if (strcmp(key, CONF_ALARM2_MINUTE) == 0 || strcmp(key, CONF_ALARM2_HOUR) == 0 || strcmp(key, CONF_ALARM2_DAY) == 0) {
            alarm2_conf_changed_pending = true;
		}
	}
	if (alarm1_conf_changed_pending || alarm2_conf_changed_pending) {
        alarm_conf_apply_after = make_timeout_time_ms(RTC_ALARM_CONF_APPLY_DELAY_MS);
        alarm_conf_apply_scheduled = true;
    }
}


/**
 * Initialize the RTC
 *
 * @param callback The callback function for alarm
 */ 
void rtc_init(gpio_event_callback_t callback) {

    powman_timer_start();

    /*
     * Recover RX8025 only when its retained data may be unreliable.
     * VLF indicates that clock operation or internal data may have
     * been affected by a supply voltage drop.
     */
    uint8_t status =
        get_virtual_register(I2C_VREG_RX8025_FLAG_REGISTER);

    if (status & BIT_VALUE(VLF)) {

        set_virtual_register(
            I2C_VREG_RX8025_CONTROL_REGISTER,
            BIT_VALUE(RESET)
        );

        /*
         * Clearing the flag register is appropriate here because VLF
         * indicates that the retained RTC state is not reliable.
         */
        set_virtual_register(
            I2C_VREG_RX8025_FLAG_REGISTER,
            0x00
        );

        // A restored alarm cannot be trusted after RTC voltage loss.
        alarm_type = ALARM_TYPE_NONE;
    }

    /*
     * Configure normal RTC operation.
     *
     * Do not clear the flag register during normal initialization.
     * AF may represent an alarm that occurred while the RP2350 was
     * waking from hibernation and must remain pending until handled.
     */
    set_virtual_register(
        I2C_VREG_RX8025_EXTENSION_REGISTER,
        BIT_VALUE(WADA)
    );

    /*
     * Only drive the RTC alarm output when a scheduled action exists.
     * The RTC may retain three AE=1 fields (alarm every minute), but
     * without an armed action they must not produce an interrupt.
     * Preserve AF here: it may represent an alarm during hibernation.
     */
    if (!rtc_set_alarm_interrupt_enabled(alarm_type != ALARM_TYPE_NONE)) {
        debug_log("Failed to configure RTC alarm interrupt.\n");
    }

    rtc_sync_powman_timer();

    rtc_alarm_callback = callback;

    gpio_init(GPIO_RTC_INT);
    gpio_set_dir(GPIO_RTC_INT, GPIO_IN);
    gpio_pull_up(GPIO_RTC_INT);
    gpio_register_callback(
        GPIO_RTC_INT,
        GPIO_IRQ_EDGE_FALL,
        rtc_alarm_occurred
    );

    add_alarm_in_us(
        SYNC_TIME_INTERVAL_US,
        sync_time_callback,
        NULL,
        true
    );

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
	    //debug_log("Write RTC time to POWMAN timer.\n");
		powman_timer_set_ms((ts + TIMESTAMP_2000_01_01) * 1000);
	} else {
	    //debug_log("Write POWMAN time to RTC.\n");
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
 * Enable or disable the RX8025 alarm interrupt output.
 * An alarm may still set AF while AIE is disabled.
 */
static bool rtc_set_alarm_interrupt_enabled(bool enabled) {
    uint8_t control = INT_2_SEC | (enabled ? BIT_VALUE(AIE) : 0);
    return set_virtual_register(I2C_VREG_RX8025_CONTROL_REGISTER, control) == 1;
}

static bool rtc_clear_alarm_flag_checked(void) {
    uint8_t status;
    if (i2c_read_from_slave(RX8025_ADDRESS, RX8025_RTC_FLAG, &status, 1) != 1)
        return false;
    status &= (uint8_t)~BIT_VALUE(AF);
    return i2c_write_to_slave(RX8025_ADDRESS, RX8025_RTC_FLAG, &status, 1) == 1;
}

void rtc_clear_alarm_flag(void) {
    (void)rtc_clear_alarm_flag_checked();
}

static bool rtc_update_alarm(int8_t day, int8_t hour, int8_t min, bool startup) {
    const uint8_t requested_type = startup ? ALARM_TYPE_STARTUP : ALARM_TYPE_SHUTDOWN;
    const bool cancel = day == 0 && hour == 0 && min == 0;

    if (cancel && alarm_type != ALARM_TYPE_NONE && alarm_type != requested_type)
        return true;

    if (!cancel &&
        (day < -1 || day > 31 || hour < -1 || hour > 23 ||
         min < -1 || min > 59 ||
         (day == -1 && hour == -1 && min == -1) || day == 0)) {
        debug_log("Reject invalid RTC alarm parameters.\n");
        return false;
    }
    if (!rtc_set_alarm_interrupt_enabled(false)) {
        debug_log("Failed to disable RTC alarm interrupt.\n");
        return false;
    }
    alarm_type = ALARM_TYPE_NONE;
    if (cancel) {
        if (!rtc_clear_alarm_flag_checked()) {
            debug_log("Failed to clear cancelled RTC alarm flag.\n");
            return false;
        }
        return true;
    }
    if (set_virtual_register(I2C_VREG_RX8025_DAY_ALARM,
                             day >= 0 ? dec_to_bcd(day) : AE_MASK) != 1 ||
        set_virtual_register(I2C_VREG_RX8025_HOUR_ALARM,
                             hour >= 0 ? dec_to_bcd(hour) : AE_MASK) != 1 ||
        set_virtual_register(I2C_VREG_RX8025_MIN_ALARM,
                             min >= 0 ? dec_to_bcd(min) : AE_MASK) != 1 ||
        !rtc_clear_alarm_flag_checked() ||
        !rtc_set_alarm_interrupt_enabled(true)) {
        rtc_set_alarm_interrupt_enabled(false);
        debug_log("Failed to program RTC alarm.\n");
        return false;
    }
    alarm_type = requested_type;
    return true;
}

void rtc_set_alarm(int8_t day, int8_t hour, int8_t min, bool startup) {
    (void)rtc_update_alarm(day, hour, min, startup);
}


/**
 * Get actual scheduled time by day, hour and minute values
 *
 * @param day The scheduled day
 * @param hour The scheduled hour
 * @param min The scheduled minute
 * @param sec The scheduled second
 * @param dt Pointer to DateTime object that stores the time
 * @return true if succeed, otherwise false
 */
bool rtc_get_scheduled_time(int8_t day, int8_t hour, int8_t min, int8_t sec, DateTime *dt) {
    if (!dt) {
        return false;
    }

    day &= 0x7F;
    hour &= 0x7F;
    min &= 0x7F;
    sec &= 0x7F;
    if (day < 1 || day > 31 || hour > 23 || min > 59 || sec > 59) {
        return false;
    }

    DateTime now;
    if (!rtc_get_time(&now)) return false;

    DateTime candidate = now;
    candidate.day = day;
    candidate.hour = hour;
    candidate.min = min;
    candidate.sec = sec;

    int64_t now_std;
    if (!dst_get_standard_now(&now_std)) return false;
    while (
        day > get_days_in_month(candidate.year, candidate.month) ||
        dst_action_to_standard(get_total_seconds(&candidate)) <= now_std
    ) {
        candidate.month++;
        if (candidate.month > 12) {
            candidate.month = 1;
            candidate.year++;
            if (candidate.year > 2099) return false;
        }
        candidate.day = day;
        candidate.hour = hour;
        candidate.min = min;
        candidate.sec = sec;
    }
    *dt = candidate;
    return true;
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
 * Process a DST transition only after the continuous standard timeline
 * reaches its transition timestamp. This never repeats the autumn rollback.
 */
bool apply_dst_if_needed(void) {
    return dst_process_due_transition();
}


/**
 * Load scheduled shutdown/startup from configuration and set it to RTC alarm
 *
 * @param startup Whether it is for startup (false for shutdown)
 *
 * @return true if alarm set succesfully, otherwise false
 */
bool load_and_schedule_alarm(bool startup) {
    bool valid;
    int64_t cur_ts = rtc_get_timestamp(&valid);
    if (!valid) {
        return false;
    }

    uint8_t sec = bcd_to_dec(conf_get(startup ? CONF_ALARM1_SECOND : CONF_ALARM2_SECOND));
    uint8_t min = bcd_to_dec(conf_get(startup ? CONF_ALARM1_MINUTE : CONF_ALARM2_MINUTE));
    uint8_t hour = bcd_to_dec(conf_get(startup ? CONF_ALARM1_HOUR : CONF_ALARM2_HOUR));
    uint8_t day = bcd_to_dec(conf_get(startup ? CONF_ALARM1_DAY : CONF_ALARM2_DAY));

    DateTime dt;
    if (!rtc_get_scheduled_time(day, hour, min, sec, &dt)) {
        return false;
    }

    int64_t scheduled_ts = dst_action_to_hardware_local(get_total_seconds(&dt));
    timestamp_to_datetime(scheduled_ts, &dt);
    if (!rtc_update_alarm(dt.day, dt.hour, dt.min, startup)) return false;
    debug_log("Set Alarm %02d %02d:%02d for %s\n",
              dt.day, dt.hour, dt.min, startup ? "startup" : "shutdown");

    schedule_mark_processed_this_boot();
    return true;
}


/**
 * Restore alarm type after waking up from hibernation
 *
 * @param type Alarm type to restore
 */
void rtc_restore_alarm_type_after_hibernate(uint8_t type) {
    if (type == ALARM_TYPE_NONE || type == ALARM_TYPE_STARTUP || type == ALARM_TYPE_SHUTDOWN) {
        alarm_type = type;
    }
}


/**
 * Process pending alarm configuration at correct moment
 */
void rtc_process_pending_alarm_conf(void) {
    if (!alarm_conf_apply_scheduled || !time_reached(alarm_conf_apply_after)) {
        return;
    }

    alarm_conf_apply_scheduled = false;

    if (alarm1_conf_changed_pending) {
        alarm1_conf_changed_pending = false;
        rtc_apply_alarm1_conf();
    }

    if (alarm2_conf_changed_pending) {
        alarm2_conf_changed_pending = false;
        rtc_apply_alarm2_conf();
    }
}


/**
 * Check if there is alarm currently pending
 *
 * @return true if alarm is pending, otherwise false
 */
bool rtc_is_alarm_pending(void) {
    uint8_t status = get_virtual_register(I2C_VREG_RX8025_FLAG_REGISTER);
    return (status & BIT_VALUE(AF)) != 0;
}