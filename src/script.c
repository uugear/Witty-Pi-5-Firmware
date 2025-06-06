#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ff.h>

#include "main.h"
#include "script.h"
#include "log.h"
#include "fatfs_disk.h"
#include "conf.h"
#include "util.h"


#define WPI_SCRIPT_STATE_ON		0
#define WPI_SCRIPT_STATE_OFF	1
#define WPI_MAX_LINES 			128
#define WPI_MAX_LINE_LENGTH 	128
#define WPI_MAX_ACTIONS 		4096

#define ACT_MAX_LINE_LENGTH     32

#define SKD_MAX_LINE_LENGTH     32


typedef struct {
	int8_t type;		// WPI_SCRIPT_STATE_ON or WPI_SCRIPT_STATE_OFF
	int64_t duration;	// Duration in seconds
} StateInfo;


bool script_in_use = false;


// Parse YYYY-MM-DD HH:mm:ss string to DateTime
bool str_to_datetime(const char* str, DateTime* dt) {
    int year, month, day, hour, min, sec;
    if (sscanf(str, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) != 6) {
        return false;
    }
    dt->year = year;
    dt->month = month;
    dt->day = day;
    dt->hour = hour;
    dt->min = min;
    dt->sec = sec;
    return true;
}


// Parse time component, such as D1, H2, M25, S15 etc.
bool parse_time_component(const char* str, int* hours, int* minutes, int* seconds) {
    if (strlen(str) < 2) return false;
    
    char unit = str[0];
    int value = atoi(str + 1);
    
    if (value < 0) return false;
    
    switch (unit) {
        case 'D':
        case 'd':
            *hours += (value * 24);
            break;
        case 'H':
        case 'h':
            *hours += value;
            break;
        case 'M':
        case 'm':
            *minutes += value;
            break;
        case 'S':
        case 's':
            *seconds += value;
            break;
        default:
            return false;
    }
    return true;
}


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
bool parse_wpi_script(const char* script_text, Action* actions, int* num_actions, int64_t cur_time) {
    DateTime begin_dt = {0};
    DateTime end_dt = {0};
    int64_t begin_time = 0;
    int64_t end_time = 0;
    
    StateInfo states[WPI_MAX_LINES];
    int state_count = 0;
    bool begin_found = false;
    bool end_found = false;
    
    char line_buffer[WPI_MAX_LINE_LENGTH];
    
    // First go: extract BEGIN, END and state definitions
    const char* line_start = script_text;
    const char* next_line = NULL;
    
    while (line_start && *line_start) {
        // Copy a line to buffer
        next_line = strchr(line_start, '\n');
        size_t line_length = next_line ? (next_line - line_start) : strlen(line_start);
        if (line_length >= WPI_MAX_LINE_LENGTH) {
            debug_log("Error: Line too long\n");
            return false;
        }
        strncpy(line_buffer, line_start, line_length);
        line_buffer[line_length] = '\0';
        
        // Remove comment
        char* comment = strchr(line_buffer, '#');
        if (comment) *comment = '\0';
        
        // Trim leading white spaces
        char* trimmed = line_buffer;
        while (isspace(*trimmed)) trimmed++;
        
        // Skip empty line
        if (*trimmed != '\0') {
            // Extract BEGIN or END
            if (strncmp(trimmed, "BEGIN", 5) == 0) {
                begin_found = str_to_datetime(trimmed + 5, &begin_dt);
                if (begin_found) {
                    begin_time = get_total_seconds(&begin_dt);
                }
            } 
            else if (strncmp(trimmed, "END", 3) == 0) {
                end_found = str_to_datetime(trimmed + 3, &end_dt);
                if (end_found) {
                    end_time = get_total_seconds(&end_dt);
                }
            }
            // Parse state definition
            else if (strncmp(trimmed, "ON", 2) == 0 || strncmp(trimmed, "OFF", 3) == 0) {
                StateInfo state = {0};
                if (trimmed[1] == 'N') {
                    state.type = WPI_SCRIPT_STATE_ON;
                    trimmed += 2;
                } else {
                    state.type = WPI_SCRIPT_STATE_OFF;
                    trimmed += 3;
                }
                
                // Skip white spaces
                while (isspace(*trimmed)) trimmed++;
                
                // Parse time components
                int hours = 0, minutes = 0, seconds = 0;
                char token_buffer[WPI_MAX_LINE_LENGTH];
                const char* token_start = trimmed;
                
                while (*token_start) {
                    // Skip leading whitespace
                    while (isspace(*token_start)) token_start++;
                    if (*token_start == '\0') break;
                    
                    // Find token end
                    const char* token_end = token_start;
                    while (*token_end && !isspace(*token_end)) token_end++;
                    
                    // Extract token to buffer
                    size_t token_len = token_end - token_start;
                    if (token_len >= WPI_MAX_LINE_LENGTH) {
                        debug_log("Error: Token too long\n");
                        return false;
                    }
                    
                    strncpy(token_buffer, token_start, token_len);
                    token_buffer[token_len] = '\0';
                    
                    if (!parse_time_component(token_buffer, &hours, &minutes, &seconds)) {
                        debug_log("Error: Invalid time component '%s'\n", token_buffer);
                        return false;
                    }
                    
                    // Move to next token
                    token_start = token_end;
                }
                
                // Calculate total seconds
                state.duration = hours * 3600LL + minutes * 60LL + seconds;
                
                // Append to state list
                if (state_count < WPI_MAX_LINES) {
                    states[state_count++] = state;
                } else {
                    debug_log("Error: Too many states defined\n");
                    return false;
                }
            }
        }
        
        // Move to next line
        if (next_line) {
            line_start = next_line + 1;
        } else {
            break;
        }
    }
    
    // Check if the script is good
    if (!begin_found || !end_found || state_count == 0) {
        debug_log("Error: Missing required BEGIN, END or state definitions\n");
        return false;
    }

    // Calculate cycle period and skip cycles in the past (shift the BEGIN time)
    int64_t current_time = begin_time;
    int64_t period = 0;
    for (int i = 0; i < state_count; i++) {
        period += states[i].duration;
    }
    while (current_time + period <= cur_time) {
        current_time += period;
    }
    
    // Generate action list
    *num_actions = 0;
    bool is_on = false;
    
    // Append the first UP action for the (shifted) BEGIN moment
    actions[(*num_actions)++] = (Action){.is_up = true, .time = current_time};
    is_on = true;
    
    // Generate actions based on state changes, until the END moment
    while (current_time < end_time && *num_actions < WPI_MAX_ACTIONS - 1) {
        for (int i = 0; i < state_count; i++) {
            current_time += states[i].duration;
            
            if (current_time >= end_time) {
                // If device is currently on, add one last DOWN action
                if (is_on) {
                    // Use end_time instead of potentially going beyond it
                    actions[(*num_actions)++] = (Action){.is_up = false, .time = end_time};
                }
                break;
            }
            
            if (states[i].type == WPI_SCRIPT_STATE_ON) {
                // ON state ends, add DOWN action
                actions[(*num_actions)++] = (Action){.is_up = false, .time = current_time};
                is_on = false;
            } else {
                // OFF state ends, add UP action
                actions[(*num_actions)++] = (Action){.is_up = true, .time = current_time};
                is_on = true;
            }
        }
    }
    if (*num_actions >= WPI_MAX_ACTIONS - 1) {
        debug_log("Warning: action list is truncated.\n");
    }
    return true;
}


// Save action/alarm to configuration
bool configure_action(Action * action) {
    if(!action) {
        return false;
    }
    DateTime dt;
	timestamp_to_datetime(action->time, &dt);
	
    if (action->is_up) {
        conf_set(CONF_ALARM1_SECOND, dec_to_bcd(dt.sec));
        conf_set(CONF_ALARM1_MINUTE, dec_to_bcd(dt.min));
        conf_set(CONF_ALARM1_HOUR, dec_to_bcd(dt.hour));
        conf_set(CONF_ALARM1_DAY, dec_to_bcd(dt.day));
    } else {
        conf_set(CONF_ALARM2_SECOND, dec_to_bcd(dt.sec));
        conf_set(CONF_ALARM2_MINUTE, dec_to_bcd(dt.min));
        conf_set(CONF_ALARM2_HOUR, dec_to_bcd(dt.hour));
        conf_set(CONF_ALARM2_DAY, dec_to_bcd(dt.day));
    }
    return true;
}


// Set RTC alarm for action, also save to configuration
bool set_alarm_for_action(Action * action) {
    if(!action) {
        return false;
    }
    DateTime dt;
	timestamp_to_datetime(action->time, &dt);
	debug_log("%s is scheduled to: %d-%02d-%02d %02d:%02d:%02d\n", action->is_up ? "Startup" : "Shutdown", dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
    
    rtc_set_alarm(dt.day, dt.hour, dt.min, action->is_up);
    
    if (action->is_up) {
        conf_set(CONF_ALARM1_SECOND, dec_to_bcd(dt.sec));
        conf_set(CONF_ALARM1_MINUTE, dec_to_bcd(dt.min));
        conf_set(CONF_ALARM1_HOUR, dec_to_bcd(dt.hour));
        conf_set(CONF_ALARM1_DAY, dec_to_bcd(dt.day));
    } else {
        conf_set(CONF_ALARM2_SECOND, dec_to_bcd(dt.sec));
        conf_set(CONF_ALARM2_MINUTE, dec_to_bcd(dt.min));
        conf_set(CONF_ALARM2_HOUR, dec_to_bcd(dt.hour));
        conf_set(CONF_ALARM2_DAY, dec_to_bcd(dt.day));
    }
    return true;
}


/**
 * Find future startup and shutdown Action from .skd file
 * 
 * @param path The path of .skd file
 * @param cur_time The current timestamp (total seconds since year 2000)
 * @param startup_first true if find startup Action first, false otherwise
 * @param startup The pointer to startup Action object
 * @param shutdown The pointer to shutdown Action object
 * @return true if both actions are found, false otherwise
 */
bool find_next_actions_from_skd(const char *path, uint64_t cur_time, bool startup_first, Action *startup, Action *shutdown) {
    FIL file;
    FRESULT fr;
    char line_buffer[128];
    UINT bytes_read;
    bool found_startup = false;
    bool found_shutdown = false;
    
    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        return false;
    }
    
    while (f_read_line(line_buffer, sizeof(line_buffer), &file)) {
        char *line = line_buffer;
        
        // Skip whitespace at the beginning of the line
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        
        // Skip comment lines (starting with #)
        if (*line == '#' || *line == '\n' || *line == '\0') {
            continue;
        }
        
        // Read action type (U/D)
        char action_type = *line++;
        bool is_up;
        
        if (action_type == 'U') {
            is_up = true;
        } else if (action_type == 'D') {
            is_up = false;
        } else {
            continue;
        }
        
        // Parse timestamp
        uint64_t timestamp = 0;
        while (isdigit(*line)) {
            timestamp = timestamp * 10 + (*line - '0');
            line++;
        }
        
        // Skip if timestamp is in the past
        if (timestamp <= cur_time) {
            continue;
        }
        
        if (startup_first) {
            // Looking for startup first, then shutdown
            if (!found_startup && is_up) {
                if (startup != NULL) {
                    startup->is_up = true;
                    startup->time = timestamp;
                }
                found_startup = true;
            } else if (found_startup && !is_up && timestamp > startup->time) {
                if (shutdown != NULL) {
                    shutdown->is_up = false;
                    shutdown->time = timestamp;
                }
                found_shutdown = true;
                break;
            }
        } else {
            // Looking for shutdown first, then startup
            if (!found_shutdown && !is_up) {
                if (shutdown != NULL) {
                    shutdown->is_up = false;
                    shutdown->time = timestamp;
                }
                found_shutdown = true;
            } else if (found_shutdown && is_up && timestamp > shutdown->time) {
                if (startup != NULL) {
                    startup->is_up = true;
                    startup->time = timestamp;
                }
                found_startup = true;
                break;
            }
        }
    }
    
    f_close(&file);
    
    return (found_startup && found_shutdown);
}


/**
 * Convert an .act script file to a more compact .skd format
 * 
 * @param act_script_path Path to the source .act file
 * @param skd_script_path Path to the .skd file to be created
 * @return true if conversion was successful, false otherwise
 */
bool convert_act_to_skd(const char* act_script_path, const char* skd_script_path) {
    FIL act_file, skd_file;
    FRESULT fr;
    char line_buffer[ACT_MAX_LINE_LENGTH];
    UINT bytes_read, bytes_written;
    char action_str[3];
    char datetime_str[20];
    char output_buffer[SKD_MAX_LINE_LENGTH];
    DateTime dt;
    uint64_t timestamp;
    
    fr = f_open(&act_file, act_script_path, FA_READ);
    if (fr != FR_OK) {
        return false;
    }
    
    fr = f_open(&skd_file, skd_script_path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        f_close(&act_file);
        return false;
    }
    
    // Write header comment
    const char* header = "# Converted from .act script\n\n";
    fr = f_write(&skd_file, header, strlen(header), &bytes_written);
    if (fr != FR_OK || bytes_written != strlen(header)) {
        f_close(&skd_file);
        return false;
    }
    
    while (f_read_line(line_buffer, sizeof(line_buffer), &act_file)) {
        // Skip leading whitespace
        char* line = line_buffer;
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        
        // Skip comment line or empty line
        if (*line == '#' || *line == '\n' || *line == '\r' || *line == '\0') {
            continue;
        }
        
        // Read action type (UP/DN)
        if (sscanf(line, "%2s", action_str) != 1) {
            continue;
        }
        
        // Determine action character
        char action_char;
        if (strcmp(action_str, "UP") == 0) {
            action_char = 'U';
        } else if (strcmp(action_str, "DN") == 0) {
            action_char = 'D';
        } else {
            continue;
        }
        
        // Find the datetime portion
        char* datetime_start = line + 2;  // Skip "UP" or "DN"
        
        // Skip whitespace
        while (*datetime_start == ' ' || *datetime_start == '\t') {
            datetime_start++;
        }
        
        // Extract the datetime string
        int dt_pos = 0;
        while (datetime_start[dt_pos] != '\0' && 
               datetime_start[dt_pos] != '\n' && 
               datetime_start[dt_pos] != '\r' && 
               datetime_start[dt_pos] != '#' && 
               dt_pos < 19) {
            datetime_str[dt_pos] = datetime_start[dt_pos];
            dt_pos++;
        }
        datetime_str[dt_pos] = '\0';
        
        // Parse the datetime
        if (!str_to_datetime(datetime_str, &dt)) {
            continue;
        }
        
        // Convert to timestamp
        timestamp = get_total_seconds(&dt);
        
        // Find any comment after the datetime
        char* comment = NULL;
        char* line_ptr = datetime_start + dt_pos;
        while (*line_ptr != '\0' && *line_ptr != '#') {
            line_ptr++;
        }
        
        if (*line_ptr == '#') {
            comment = line_ptr;
        }
        
        // Format the output line: action character followed by timestamp
        int written;
        if (comment) {
            written = snprintf(output_buffer, sizeof(output_buffer), 
                              "%c%llu %s", action_char, timestamp, comment);
        } else {
            written = snprintf(output_buffer, sizeof(output_buffer), 
                              "%c%llu\n", action_char, timestamp);
        }
        
        if (written < 0 || written >= sizeof(output_buffer)) {
            f_close(&act_file);
            f_close(&skd_file);
            return false;
        }
        
        // Write the line to the output file
        fr = f_write(&skd_file, output_buffer, strlen(output_buffer), &bytes_written);
        if (fr != FR_OK || bytes_written != strlen(output_buffer)) {
            f_close(&act_file);
            f_close(&skd_file);
            return false;
        }
    }
    
    f_close(&act_file);
    f_close(&skd_file);
    return true;
}


/**
 * Convert a .wpi script file to a .act script format
 * 
 * @param wpi_script_path Path to the source .wpi file
 * @param act_script_path Path to the .act file to be created
 * @param cur_time Timestamp for current time
 * @return true if conversion was successful, false otherwise
 */
bool convert_wpi_to_act(const char* wpi_script_path, const char* act_script_path, int64_t cur_time) {
    FIL wpi_file, act_file;
    FRESULT fr;
    char line_buffer[WPI_MAX_LINE_LENGTH];
    UINT bytes_read, bytes_written;
    char output_buffer[ACT_MAX_LINE_LENGTH];
    
    // Load the .wpi file content to RAM
    char* wpi_content = NULL;
    size_t wpi_size = 0;
    Action actions[WPI_MAX_ACTIONS];
    int num_actions = 0;
    
    fr = f_open(&wpi_file, wpi_script_path, FA_READ);
    if (fr != FR_OK) {
        return false;
    }
    
    FILINFO fno;
    fr = f_stat(wpi_script_path, &fno);
    if (fr != FR_OK) {
        f_close(&wpi_file);
        return false;
    }
    
    wpi_size = fno.fsize;
    wpi_content = (char*)malloc(wpi_size + 1);
    if (wpi_content == NULL) {
        f_close(&wpi_file);
        return false;
    }
    
    fr = f_read(&wpi_file, wpi_content, wpi_size, &bytes_read);
    if (fr != FR_OK || bytes_read != wpi_size) {
        free(wpi_content);
        f_close(&wpi_file);
        return false;
    }
    wpi_content[wpi_size] = '\0';
    
    f_close(&wpi_file);
    
    // Parse the .wpi script to generate actions
    if (!parse_wpi_script(wpi_content, actions, &num_actions, cur_time)) {
        free(wpi_content);
        return false;
    }
    free(wpi_content);
    
    // Create/open the output .act file
    fr = f_open(&act_file, act_script_path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        return false;
    }
    
    // Write header comment
    const char* header = "# Converted from .wpi script\n\n";
    fr = f_write(&act_file, header, strlen(header), &bytes_written);
    if (fr != FR_OK || bytes_written != strlen(header)) {
        f_close(&act_file);
        return false;
    }
    
    // Process each action and write to .act file
    for (int i = 0; i < num_actions; i++) {
        DateTime dt;
        timestamp_to_datetime(actions[i].time, &dt);
        
        // Format datetime string
        char datetime_str[20];
        snprintf(datetime_str, sizeof(datetime_str), 
                "%04d-%02d-%02d %02d:%02d:%02d", 
                dt.year, dt.month, dt.day, 
                dt.hour, dt.min, dt.sec);
        
        // Format the action line
        int written = snprintf(output_buffer, sizeof(output_buffer),
                              "%s %s\n", 
                              actions[i].is_up ? "UP" : "DN", 
                              datetime_str);
        
        if (written < 0 || written >= sizeof(output_buffer)) {
            f_close(&act_file);
            return false;
        }
        
        // Write the line to the output file
        fr = f_write(&act_file, output_buffer, strlen(output_buffer), &bytes_written);
        if (fr != FR_OK || bytes_written != strlen(output_buffer)) {
            f_close(&act_file);
            return false;
        }
    }
    
    f_close(&act_file);
    return true;
}


/**
 * Remove schedule.wpi, schedule.act and schedule.skd files, if any of them exists
 * This function will not change RTC alarm settings, but will mark script "not in used"
 */
void purge_script(void) {
    file_delete(WPI_SCRIPT_PATH);
    file_delete(ACT_SCRIPT_PATH);
    file_delete(SKD_SCRIPT_PATH);
    set_script_in_use(false);
}


/**
 * Try to load schedule script (schedule.skd file) and optionally run it.
 * When schedule.skd file is not found, try to generate it from schedule.act file
 * When schedule.act file is not found, try to generate it from schedule.wpi file.
 * 
 * @param run Whether to run the schedule script
 * 
 * @return true if schedule script loads (and runs) successfully, false otherwise
 */
bool load_script(bool run) {
    
    set_script_in_use(false);
    
    bool valid;
    uint64_t cur_time = rtc_get_timestamp(&valid);
    if (!valid) {
        debug_log("Current time is invalid, skip schedule script.\n");
        return false;
    }
    
    if (!file_exists(SKD_SCRIPT_PATH)) {
        if (file_exists(ACT_SCRIPT_PATH)) {
            if (convert_act_to_skd(ACT_SCRIPT_PATH, SKD_SCRIPT_PATH)) {
                debug_log("Generated .skd file from .act file\n");
            } else {
                debug_log("Failed to generate .skd file from .act file\n");
                return false;
            }
        } else if (file_exists(WPI_SCRIPT_PATH)) {
            if (convert_wpi_to_act(WPI_SCRIPT_PATH, ACT_SCRIPT_PATH, cur_time)) {
                debug_log("Generated .act file from .wpi file\n");
            } else {
                debug_log("Failed to generate .act file from .wpi file\n");
                return false;
            }
            if (convert_act_to_skd(ACT_SCRIPT_PATH, SKD_SCRIPT_PATH)) {
                debug_log("Generated .skd file from .act file\n");
            } else {
                debug_log("Failed to generate .skd file from .act file\n");
                return false;
            }
        } else {
            debug_log("No schedule script is found.\n");
            return false;
        }
    }
    
    if (!run) {
        set_script_in_use(true);
        return true;
    }
    
    log_current_rpi_state();
    
    Action startup;
    Action shutdown;
    bool startup_first = (current_rpi_state == STATE_STOPPING || current_rpi_state == STATE_OFF);
    bool actions_found = false;
    
    if (file_exists(SKD_SCRIPT_PATH)) {
        if (find_next_actions_from_skd(SKD_SCRIPT_PATH, cur_time, startup_first, &startup, &shutdown)) {
            debug_log("Found future actions actions from %s\n", SKD_SCRIPT_PATH);
        } else {
            debug_log("No future action is found in script.\n");
            return false;
        }
    } else {
        debug_log("The file %s is not found.\n", SKD_SCRIPT_PATH);
        return false;
    }
    
    bool success = true;
    if (startup_first) {
		adjust_action_time_for_dst(&startup.time);
        if (!set_alarm_for_action(&startup)) {
            debug_log("Can not set alarm for startup action.\n");
            success = false;
        }
        if (!configure_action(&shutdown)) {
            debug_log("Can not configure shutdown action.\n");
            success = false;
        }
    } else {
		adjust_action_time_for_dst(&shutdown.time);
        if (!set_alarm_for_action(&shutdown)) {
            debug_log("Can not set alarm for shutdown action.\n");
            success = false;
        }
        if (!configure_action(&startup)) {
            debug_log("Can not configure startup action.\n");
            success = false;
        }
    }
	
	set_script_in_use(success);
	
    return success;
}


/**
 * Mark or clear the "script in used" state
 * 
 * @param in_use true to mark the "in used" state, false to clear it
 */
void set_script_in_use(bool in_use) {
    script_in_use = in_use;
}


/**
 * Whether the schedule script is currently in use
 * 
 * @return true or false
 */
bool is_script_in_use(void) {
	return script_in_use;
}