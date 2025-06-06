#ifndef _RTC_H_
#define _RTC_H_

#include <stdbool.h>
#include <time.h>

#include "gpio.h"


#define GPIO_RTC_INT	8


#define ALARM_TYPE_NONE      0
#define ALARM_TYPE_STARTUP   1
#define ALARM_TYPE_SHUTDOWN  2


typedef struct {
    int16_t year;   // 2000~2099
    int8_t month;   // 1~12
    int8_t day;     // 1~28/29/30/31
    int8_t hour;    // 0~23
    int8_t min;     // 0~59
    int8_t sec;     // 0~59
    int8_t wday;    // 0~6 (Sunday~Saturday)
} DateTime;


/**
 * Check if given year is leap year
 * 
 * @param year The year
 * @return true if the year is a leap year, false otherwise
 */
bool is_leap_year(int year);


/**
 * Get the number of days in given month
 * 
 * @param year The year of the month
 * @param month The month (1~12)
 * @return The timestamp (total seconds since year 2000)
 */
int get_days_in_month(int year, int month);


/**
 * Convert DateTime to timestamp
 * 
 * @param dt Pointer to DateTime struct
 * @return The timestamp (total seconds since year 2000)
 */
int64_t get_total_seconds(DateTime *dt);


/**
 * Convert timestamp to DateTime
 * 
 * @param timestamp The timestamp (total seconds since year 2000)
 * @param dt Pointer to DateTime struct
 * @return true if converted successful, false otherwise
 */
void timestamp_to_datetime(int64_t timestamp, DateTime *dt);


/**
 * Initialize the RTC
 *
 * @param callback The callback function for alarm
 */                                                      
void rtc_init(gpio_event_callback_t callback);


/**
 * Synchronize the powman timer with RTC
 */
void rtc_sync_powman_timer(void);


/**
 * Get RTC time
 *
 * @param time_info Pointer to tm structure who stores the time information
 * @return true if the retrieved time is valid, false otherwise
 */
bool rtc_get_time(DateTime *dt);


/**
 * Get RTC timestamp
 *
 * @param time_info Pointer to tm structure who stores the time information
 * @return the total seconds since year 2000
 */
int64_t rtc_get_timestamp(bool *valid);


/**
 * Set RTC time
 *
 * @param dt Pointer to DateTime object
 * @return true if set successfully, false otherwise
 */
bool rtc_set_time(DateTime *dt);


/**
 * Set RTC timestamp
 *
 * @param timestamp The total seconds since year 2000
 * @return true if set successfully, otherwise false
 */
bool rtc_set_timestamp(int64_t timestamp);


/**
 * Clear RTC alarm flag (so alarm can occur again)
 * This function also clears the type of current scheduled alarm
 */
void rtc_clear_alarm_flag(void);


/**
 * Set RTC alarm
 *
 * @param day The day of alarm, use negative value to skip
 * @param hour The hour of alarm, use negative value to skip
 * @param min The minute of alarm, use negative value to skip
 * @param startup Whether the alarm is for startup
 * @return true if set successfully, false otherwise
 */
void rtc_set_alarm(int8_t day, int8_t hour, int8_t min, bool startup);


/**
 * Get the type of current scheduled alarm
 *
 * @return The type of current alarm
 */
uint8_t rtc_get_alarm_type(void);


/**
 * Check whether Raspberry Pi can be shut down at current time
 *
 * @return true or false
 */
bool can_cur_time_turn_off_rpi(void);


/**
 * Apply daylight-saving time (DST) if needed
 *
 * @return true if newly applied, otherwise false
 */
bool apply_dst_if_needed(void);


/**
 * Adjust given startup/shutdown timestamp for incoming DST switching
 *
 * @param p_action_ts The pointer to action timestamp
 *
 * @return true if adjustment has been made, otherwise false
 */
bool adjust_action_time_for_dst(uint64_t * p_action_ts);


/**
 * Load scheduled shutdown from configuration and set it to RTC alarm
 *
 * @param startup Whether it is for startup (false for shutdown)
 */
void load_and_schedule_alarm(bool startup);


/**
 * Get actual scheduled time by day, hour and minute values
 *
 * @param day The scheduled day
 * @param hour The scheduled hour
 * @param min The scheduled minute
 * @param sec The scheduled second
 * @param dt Pointer to DateTime object that stores the time
 */
void rtc_get_scheduled_time(int8_t day, int8_t hour, int8_t min, int8_t sec, DateTime *dt);


#endif