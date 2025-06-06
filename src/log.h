#ifndef _LOG_H_
#define _LOG_H_


/**
 * Check whether the log should be saved to file
 * 
 * @return true for saving to file, false otherwise
 */
bool is_log_saving_to_file(void);


/**
 * Set whether the log should be saved to file
 * 
 * @param s2f true for saving to file, false otherwise
 */
void log_save_to_file(bool s2f);


/**
 * Submit a log message
 * 
 * @param fmt The printf format of the message
 */
void debug_log(const char* fmt, ...);


/**
 * Print logs to serial port, save logs to file if needed
 */
void process_log_task(void);


/**
 * Save logs to file
 */
void save_logs_to_file(void);


#endif