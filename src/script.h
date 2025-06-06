#ifndef _SCRIPT_H_
#define _SCRIPT_H_

#include "rtc.h"


#define WPI_SCRIPT_PATH         "/schedule/schedule.wpi"
#define ACT_SCRIPT_PATH         "/schedule/schedule.act"
#define SKD_SCRIPT_PATH         "/schedule/schedule.skd"


typedef struct {
    bool is_up;  	// true = UP, false = DN
    uint64_t time;
} Action;


/**
 * Parse WPI script text into an Action array
 * BEGIN time will be shifted if one or more cycles are in the past
 * Skips content starting with # (comments)
 * 
 * @param script_text The script text to parse
 * @param actions Pointer to an array where parsed actions will be stored
 * @param num_actions Pointer to store the number of actions parsed
 * @param cur_time Timestamp for current time
 * @return true if parsing was successful, false otherwise
 */
bool parse_wpi_script(const char* script_text, Action* actions, int* num_actions, int64_t cur_time);


/**
 * Find future startup and shutdown Action from .skd file
 * 
 * @param path The path of .skd file
 * @param cur_time The current timestamp (total seconds since year 2000)
 * @param startup_first true if find startup Action first, false otherwise
 * @param startup The pointer to startup Action object
 * @param startup The pointer to shutdown Action object
 * @return true if both actions are found, false otherwise
 */
bool find_next_actions_from_skd(const char *path, uint64_t cur_time, bool startup_first, Action *startup, Action *shutdown);


/**
 * Convert an .act script file to a more compact .skd format
 * 
 * @param act_script_path Path to the source .act file
 * @param skd_script_path Path to the .skd file to be created
 * @return true if conversion was successful, false otherwise
 */
bool convert_act_to_skd(const char* act_script_path, const char* skd_script_path);


/**
 * Convert a .wpi script file to a .act script format
 * 
 * @param wpi_script_path Path to the source .wpi file
 * @param act_script_path Path to the .act file to be created
 * @param cur_time Timestamp for current time
 * @return true if conversion was successful, false otherwise
 */
bool convert_wpi_to_act(const char* wpi_script_path, const char* act_script_path, int64_t cur_time);


/**
 * Remove schedule.wpi, schedule.act and schedule.skd files, if any of them exists
 * This function will not change RTC alarm settings, but will mark script "not in use"
 */
void purge_script(void);


/**
 * Try to load schedule script (schedule.skd file) and optionally run it.
 * When schedule.skd file is not found, try to generate it from schedule.act file
 * When schedule.act file is not found, try to generate it from schedule.wpi file.
 * 
 * @param run Whether to run the schedule script
 * 
 * @return true if schedule script loads (and runs) successfully, false otherwise
 */
bool load_script(bool run);


/**
 * Mark or clear the "script in use" state
 * 
 * @param in_use true to mark the "script in use" state, false to clear it
 */
void set_script_in_use(bool in_use);


/**
 * Whether the schedule script is currently in use
 * 
 * @return true or false
 */
bool is_script_in_use(void);


#endif