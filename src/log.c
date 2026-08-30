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
#include "i2c.h"
#include "file_admin.h"

#define MAX_MESSAGE_SIZE    256
#define BUFFER_SIZE         8192
#define BUFFER_MASK         (BUFFER_SIZE - 1)

#define TIME_HEADER_SIZE    21  // [MM-DD HH:mm:ss.SSS]

#define LOG_FILE_PATH       "/log/WittyPi5.log"

#define SUPPRESS_LOG_FILE_SAVING_US     5000000

#define LOG_SAVE_I2C_QUIET_MS           10

#define LOG_DOWNLOAD_STREAM_IDLE_MS     500

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

static bool is_log_file_save_urgent(void) {
    uint32_t pending_file_bytes = log_buffer.write_idx - log_buffer.file_idx;

    return pending_file_bytes > BUFFER_SIZE - MAX_MESSAGE_SIZE;
}

static void ms_timestamp_to_str(int64_t ms_timestamp, char *buf) {
    int32_t millisec = ms_timestamp % 1000;
    DateTime dt;
    timestamp_to_datetime(ms_timestamp / 1000 - TIMESTAMP_2000_01_01, &dt);
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
    if (!is_log_saving_to_file()) {
        /*
         * Logs generated while file logging is disabled are serial-only.
         * Do not keep them as a backlog for a future re-enable.
         */
        log_buffer.file_idx = write_idx;
    } else {
        bool emergency_save = is_log_file_save_urgent();
        bool i2c_quiet = i2c_external_slave_is_quiet(LOG_SAVE_I2C_QUIET_MS);
        bool download_stream_pending = i2c_download_stream_has_pending_data();
        bool download_session_active = file_admin_download_is_active();
        bool download_stream_protected =
            download_session_active || download_stream_pending;
        bool download_stream_save_allowed =
            !download_stream_protected ||
            i2c_external_slave_is_quiet(LOG_DOWNLOAD_STREAM_IDLE_MS);
        bool emergency_save_allowed = download_stream_save_allowed;
        bool normal_save =
            download_stream_save_allowed &&
            !is_usb_msc_device_mounted() &&
            i2c_quiet;

        if (get_absolute_time() >= SUPPRESS_LOG_FILE_SAVING_US &&
                ((emergency_save && emergency_save_allowed) ||
                 normal_save)) {
            save_logs_to_file();
        }
    }

    __dmb();
    log_buffer.read_idx = read_idx;
}

/**
 * Save logs to file
 */
void save_logs_to_file(void) {

    uint32_t write_idx = log_buffer.write_idx;
    uint32_t start_idx = log_buffer.file_idx;
    uint32_t available = write_idx - start_idx;

    if (available == 0) {
        return;
    }

    if (available > BUFFER_SIZE) {
        printf(
            "Log file backlog overflow, discarding %u bytes.\n",
            available
        );
        log_buffer.file_idx = write_idx;
        return;
    }

    /*
     * Keep the external I2C slave unavailable for the entire filesystem
     * operation. flash_fatfs_write() may acquire nested pauses while
     * programming individual flash sectors.
     */
    bool i2c_pause_acquired = i2c_external_slave_pause();

    static FIL fp = {0};

    FRESULT res = f_open(&fp, LOG_FILE_PATH, FA_OPEN_APPEND | FA_WRITE);
    if (res != FR_OK) {
        printf("Open log file failed (%u)\n", res);
        goto done;
    }

    uint32_t written_count = 0;

    for (uint32_t i = 0; i < available; i++) {
        UINT bw = 0;

        res = f_write(
            &fp,
            &log_buffer.buffer[
                (start_idx + i) & BUFFER_MASK
            ],
            1,
            &bw
        );

        if (res != FR_OK || bw != 1) {
            printf(
                "Write log file failed (%u, %u bytes written)\n",
                res,
                bw
            );
            break;
        }

        written_count++;
    }

    bool synced = false;

    if (written_count > 0) {
        res = f_sync(&fp);
        if (res == FR_OK) {
            synced = true;
        } else {
            printf("Sync log file failed (%u)\n", res);
        }
    }

    FRESULT close_res = f_close(&fp);
    if (close_res != FR_OK) {
        printf("Close log file failed (%u)\n", close_res);
    } else if (!synced && written_count > 0) {
        /*
         * f_close() performs its own f_sync(). If it succeeds after an
         * earlier f_sync() failure, the written data has been committed.
         */
        synced = true;
    }

    if (synced) {
        log_buffer.file_idx = start_idx + written_count;
    }

done:
    if (i2c_pause_acquired) {
        i2c_external_slave_resume();
    }
}

/**
 * Save pending logs if the log buffer is close to full.
 *
 * This function may temporarily pause the external I2C slave and should
 * only be called from a point where such a pause is safe.
 */
void save_logs_to_file_if_urgent(void) {
    if (!is_log_saving_to_file() ||
        get_absolute_time() < SUPPRESS_LOG_FILE_SAVING_US ||
        !is_log_file_save_urgent()) {
        return;
    }

    save_logs_to_file();
}