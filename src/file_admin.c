#include <stdio.h>
#include <string.h>
#include <ff.h>

#include "file_admin.h"
#include "i2c.h"
#include "usb_msc_device.h"
#include "fatfs_disk.h"
#include "script.h"
#include "log.h"

#define DIRECTORY_SCHEDULE 4


/**
 * Check if content contains protocol delimiter characters
 */
static bool content_has_delimiters(const char *content, int len) {
    for (int i = 0; i < len; i++) {
        if (content[i] == '<' || content[i] == '>' || content[i] == '|') {
            return true;
        }
    }
    return false;
}


/**
 * Check if filename is a protected schedule file.
 * Only the exact active script filenames are protected, not arbitrary "schedule.*" files.
 */
static bool is_protected_schedule_file(const char *filename) {
    return (strcmp(filename, "schedule.wpi") == 0 ||
            strcmp(filename, "schedule.act") == 0 ||
            strcmp(filename, "schedule.skd") == 0);
}


/**
 * Build filepath from directory index and filename
 * Returns false if path contains traversal attempts or is invalid
 */
static bool build_filepath(uint8_t dir, const char *filename, char *out, size_t out_size) {
    if (dir < 1 || dir > 4 || !filename || !out || out_size < 2) {
        return false;
    }
    if (strchr(filename, '/') != NULL || strchr(filename, '\\') != NULL) {
        return false;  // Reject path traversal
    }
    const char *dir_path = i2c_get_dir_path(dir);
    if (!dir_path) {
        return false;
    }
    int written = snprintf(out, out_size, "%s/%s", dir_path, filename);
    return (written > 0 && written < (int)out_size);
}


uint8_t file_admin_upload(uint8_t dir) {
    // Only /schedule allowed for uploads
    if (dir != DIRECTORY_SCHEDULE) {
        debug_log("Upload rejected: only /schedule allowed\n");
        return ADMIN_STATUS_INVALID_DIRECTORY;
    }

    // Get upload buffer via interface function
    uint8_t *upload_buffer = i2c_get_upload_buffer();

    // Parse packet: <filename|content|CRC>
    char *start = strchr((char*)upload_buffer, PACKET_BEGIN);
    char *end = strchr((char*)upload_buffer, PACKET_END);
    if (!start || !end || end <= start) {
        return ADMIN_STATUS_INVALID_PACKET;
    }
    start++;  // Skip '<'

    // Find first delimiter (after filename)
    char *delim1 = strchr(start, PACKET_DELIMITER);
    if (!delim1 || delim1 >= end) {
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Find second delimiter (after content, before CRC)
    char *delim2 = strchr(delim1 + 1, PACKET_DELIMITER);
    if (!delim2 || delim2 >= end) {
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Extract filename
    int name_len = delim1 - start;
    if (name_len <= 0 || name_len >= ADMIN_MAX_FILENAME_LEN) {
        return ADMIN_STATUS_INVALID_PACKET;
    }
    char filename[ADMIN_MAX_FILENAME_LEN];
    memcpy(filename, start, name_len);
    filename[name_len] = '\0';

    // Extract content
    char *content = delim1 + 1;
    int content_len = delim2 - content;
    if (content_len < 0) {
        return ADMIN_STATUS_INVALID_PACKET;
    }
    if (content_len > ADMIN_MAX_FILE_CONTENT) {
        debug_log("Upload rejected: content too large (%d > %d)\n", content_len, ADMIN_MAX_FILE_CONTENT);
        return ADMIN_STATUS_FILE_TOO_LARGE;
    }

    // Reject content with protocol delimiters
    if (content_len > 0 && content_has_delimiters(content, content_len)) {
        debug_log("Upload rejected: content contains reserved characters\n");
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Build filepath
    char filepath[ADMIN_MAX_FILEPATH_LEN];
    if (!build_filepath(dir, filename, filepath, sizeof(filepath))) {
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Ensure USB MSC is not mounted
    usb_msc_ensure_ejected();

    // Write file
    FIL file;
    if (f_open(&file, filepath, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        return ADMIN_STATUS_IO_ERROR;
    }

    UINT bw = 0;
    if (content_len > 0) {
        f_write(&file, content, content_len, &bw);
    }
    f_close(&file);

    if (bw == (UINT)content_len) {
        debug_log("Uploaded %d bytes to %s\n", content_len, filepath);
        return ADMIN_STATUS_OK;
    }
    return ADMIN_STATUS_IO_ERROR;
}


uint8_t file_admin_download(uint8_t dir) {
    // All directories allowed for download
    if (dir < 1 || dir > 4) {
        return ADMIN_STATUS_INVALID_DIRECTORY;
    }

    // Get buffers via interface functions
    uint8_t *upload_buffer = i2c_get_upload_buffer();
    uint8_t *download_buffer = i2c_get_download_buffer();

    // Extract filename using existing helper
    char filename[ADMIN_MAX_FILENAME_LEN];
    if (!i2c_unpack_filename((char*)upload_buffer, filename)) {
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Build filepath
    char filepath[ADMIN_MAX_FILEPATH_LEN];
    if (!build_filepath(dir, filename, filepath, sizeof(filepath))) {
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Check file size before reading
    FILINFO fno;
    if (f_stat(filepath, &fno) != FR_OK) {
        return ADMIN_STATUS_FILE_NOT_FOUND;
    }
    if (fno.fsize > ADMIN_MAX_FILE_CONTENT) {
        debug_log("Download rejected: file too large (%lu > %d)\n", fno.fsize, ADMIN_MAX_FILE_CONTENT);
        return ADMIN_STATUS_FILE_TOO_LARGE;
    }

    // Ensure USB MSC is not mounted
    usb_msc_ensure_ejected();

    // Clear buffer before reading to prevent partial corruption on failure
    memset(download_buffer, 0, ADMIN_MAX_FILE_CONTENT + 5);

    // Read directly into download_buffer at offset 1 (after '<')
    char *content = (char*)&download_buffer[1];
    int len = load_file(filepath, content, ADMIN_MAX_FILE_CONTENT);
    if (len < 0) {
        return ADMIN_STATUS_IO_ERROR;
    }

    // Pack in-place: download_buffer[0] is '<', content already at [1..len]
    download_buffer[0] = PACKET_BEGIN;
    uint8_t crc = i2c_calculate_crc8(download_buffer, len + 1);
    download_buffer[len + 1] = PACKET_DELIMITER;
    download_buffer[len + 2] = crc;
    download_buffer[len + 3] = PACKET_END;
    download_buffer[len + 4] = '\0';
    i2c_set_download_buffer_index(0);

    debug_log("Downloaded %d bytes from %s\n", len, filepath);
    return ADMIN_STATUS_OK;
}


uint8_t file_admin_delete(uint8_t dir) {
    // Only /schedule allowed for delete
    if (dir != DIRECTORY_SCHEDULE) {
        debug_log("Delete rejected: only /schedule allowed\n");
        return ADMIN_STATUS_INVALID_DIRECTORY;
    }

    // Get upload buffer via interface function
    uint8_t *upload_buffer = i2c_get_upload_buffer();

    // Extract filename
    char filename[ADMIN_MAX_FILENAME_LEN];
    if (!i2c_unpack_filename((char*)upload_buffer, filename)) {
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Prevent deletion of active schedule files (schedule.wpi, schedule.act, schedule.skd)
    if (is_script_in_use() && is_protected_schedule_file(filename)) {
        debug_log("Delete rejected: cannot delete active script\n");
        return ADMIN_STATUS_CANNOT_DELETE_ACTIVE;
    }

    // Build filepath
    char filepath[ADMIN_MAX_FILEPATH_LEN];
    if (!build_filepath(dir, filename, filepath, sizeof(filepath))) {
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Check file exists
    if (!file_exists(filepath)) {
        return ADMIN_STATUS_FILE_NOT_FOUND;
    }

    // Ensure USB MSC is not mounted
    usb_msc_ensure_ejected();

    // Delete file
    if (file_delete(filepath)) {
        debug_log("Deleted %s\n", filepath);
        return ADMIN_STATUS_OK;
    }
    return ADMIN_STATUS_IO_ERROR;
}
