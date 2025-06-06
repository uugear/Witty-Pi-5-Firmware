#include <stdio.h>
#include <pico/stdlib.h>
#include <hardware/sync.h>
#include <hardware/powman.h>

#include <ff.h>

#include <string.h>
#include <stdarg.h>

#include "log.h"
#include "conf.h"
#include "main.h"
#include "rtc.h"


#define MAX_MESSAGE_SIZE    256
#define BUFFER_SIZE         8192
#define BUFFER_MASK         (BUFFER_SIZE - 1)

#define TIME_HEADER_SIZE    21  // [MM-DD HH:mm:ss.SSS]

#define LOG_FILE_PATH       "/log/WittyPi5.log"

#define SUPPRESS_LOG_FILE_SAVING_US    5000000


typedef struct {
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;
    volatile uint32_t file_idx;
    char buffer[BUFFER_SIZE];
} log_buffer_t;

static log_buffer_t log_buffer = {0};

extern FATFS filesystem;


/**
 * Check whether the log should be saved to file
 * 
 * @return true for saving to file, false otherwise
 */
bool is_log_saving_to_file(void) {
	return conf_get(CONF_LOG_TO_FILE) != 0;
}


/**
 * Set whether the log should be saved to file
 * 
 * @param s2f true for saving to file, false otherwise
 */
void log_save_to_file(bool s2f) {
	conf_set(CONF_LOG_TO_FILE, s2f ? 1 : 0);
}


static void ms_timestamp_to_str(int64_t ms_timestamp, char *buf) {
    int32_t millisec = ms_timestamp % 1000;
    DateTime dt;
    timestamp_to_datetime(ms_timestamp / 1000, &dt);
	sprintf(buf, "%02d-%02d %02d:%02d:%02d.%03d", dt.month, dt.day, dt.hour, dt.min, dt.sec, millisec);
}


bool log_write(const char* data, size_t len) {
    uint32_t write_idx = log_buffer.write_idx;
    uint32_t read_idx = log_buffer.read_idx;

    if (((write_idx - read_idx) + len) >= BUFFER_SIZE) {
        return false;   // Message is too long
    }

    for (size_t i = 0; i < len; i++) {
        log_buffer.buffer[(write_idx + i) & BUFFER_MASK] = data[i];
    }

    __dmb();

    log_buffer.write_idx = write_idx + len;

    return true;
}


/**
 * Submit a log message
 * 
 * @param fmt The printf format of the message
 */
void debug_log(const char* format, ...) {
    char local_buffer[MAX_MESSAGE_SIZE + 1];
	char time_str[TIME_HEADER_SIZE];

    int64_t timestamp = powman_timer_get_ms();
    ms_timestamp_to_str(timestamp, time_str + 1);
    time_str[0] = '[';
    time_str[19] = ']';
    time_str[20] = ' ';
	memcpy(local_buffer, time_str, TIME_HEADER_SIZE);

    va_list args;
    va_start(args, format);
    int len = vsnprintf(local_buffer + TIME_HEADER_SIZE, sizeof(local_buffer) - TIME_HEADER_SIZE, format, args);
    va_end(args);

    if (len > 0) {
		int total_len = TIME_HEADER_SIZE + len;
        log_write(local_buffer, total_len);
    }
}


/**
 * Print logs to serial port, save logs to file if needed
 */
void process_log_task(void) {
    
    uint32_t read_idx = log_buffer.read_idx;
    uint32_t write_idx = log_buffer.write_idx;
    
    // print message
    if (read_idx != write_idx) {
        char bk = log_buffer.buffer[write_idx & BUFFER_MASK];
        log_buffer.buffer[write_idx & BUFFER_MASK] = '\0';
        printf("%s", &log_buffer.buffer[read_idx & BUFFER_MASK]);
        log_buffer.buffer[write_idx & BUFFER_MASK] = bk;
        read_idx = write_idx;
    }
    
    stdio_flush();

    // save to file
    if (is_log_saving_to_file() && get_absolute_time() >= SUPPRESS_LOG_FILE_SAVING_US && (!is_usb_msc_device_mounted() || log_buffer.write_idx - log_buffer.file_idx > BUFFER_SIZE - MAX_MESSAGE_SIZE)) {
        save_logs_to_file();
    }

    __dmb();
    log_buffer.read_idx = read_idx;
}


/**
 * Save logs to file
 */
void save_logs_to_file(void) {

    uint32_t available = log_buffer.write_idx - log_buffer.file_idx;

    if (available > 0) {

        static FIL fp = {0};

        FRESULT res = f_open(&fp, LOG_FILE_PATH, FA_OPEN_APPEND | FA_WRITE);
        if (res != FR_OK) {
            printf("Open log file failed (%u)\n", res);
        }
        for (size_t i = 0; i < available; i++) {

            f_write(&fp, &log_buffer.buffer[(log_buffer.file_idx + i) & BUFFER_MASK], 1, NULL);
        }

        log_buffer.file_idx = log_buffer.write_idx;

        f_sync(&fp);

        f_close(&fp);
    }
}
