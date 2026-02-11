#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ff.h>

#include "file_admin.h"
#include "i2c.h"
#include "usb_msc_device.h"
#include "fatfs_disk.h"
#include "script.h"
#include "log.h"

#define DIRECTORY_SCHEDULE 4

// Internal download session state for chunked transfers
typedef struct {
    char filepath[ADMIN_MAX_FILEPATH_LEN];  // Cached filepath
    uint32_t file_size;                      // Total file size (captured at start)
    uint32_t offset;                         // Current read position
    bool active;                             // Session in progress
} DownloadState;

static DownloadState download_state = {0};


/**
 * Convert nibble (0-15) to hex ASCII character
 */
static char nibble_to_hex(uint8_t n) {
    return (n < 10) ? ('0' + n) : ('A' + n - 10);
}


/**
 * Write 16-bit value as 4 hex ASCII characters
 */
static void uint16_to_hex4(uint16_t val, char *out) {
    out[0] = nibble_to_hex((val >> 12) & 0x0F);
    out[1] = nibble_to_hex((val >> 8) & 0x0F);
    out[2] = nibble_to_hex((val >> 4) & 0x0F);
    out[3] = nibble_to_hex(val & 0x0F);
}


/**
 * Write byte as 2 hex ASCII characters
 */
static void byte_to_hex(uint8_t byte, char *out) {
    out[0] = nibble_to_hex((byte >> 4) & 0x0F);
    out[1] = nibble_to_hex(byte & 0x0F);
}


/**
 * Check if content contains protocol delimiter characters
 */
static bool content_has_delimiters(const char *content, int len) {
    for (int i = 0; i < len; i++) {
        if (content[i] == PACKET_BEGIN || content[i] == PACKET_END || content[i] == PACKET_DELIMITER) {
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
    return (strcasecmp(filename, "schedule.wpi") == 0 ||
            strcasecmp(filename, "schedule.act") == 0 ||
            strcasecmp(filename, "schedule.skd") == 0);
}


/**
 * Only allow uploading/deleting known schedule-related file types.
 * Allowed extensions: .wpi, .act, .skd (case-insensitive).
 */
static bool is_allowed_schedule_filename(const char *filename) {
    if (filename == NULL) {
        return false;
    }
    const char *dot = strrchr(filename, '.');
    if (dot == NULL || dot[1] == '\0') {
        return false;
    }
    if (strcasecmp(dot, ".wpi") == 0) return true;
    if (strcasecmp(dot, ".act") == 0) return true;
    if (strcasecmp(dot, ".skd") == 0) return true;
    return false;
}


static int find_byte_bounded(const uint8_t *buf, size_t len, uint8_t value, size_t start_pos) {
    for (size_t i = start_pos; i < len; i++) {
        if (buf[i] == value) {
            return (int)i;
        }
    }
    return -1;
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


/**
 * Clear download session state
 */
void file_admin_clear_download_state(void) {
    memset(&download_state, 0, sizeof(download_state));
}


/**
 * Load next chunk into download buffer
 * Called by FILE_DOWNLOAD (first chunk) and FILE_DOWNLOAD_NEXT (subsequent chunks)
 *
 * Packet format: <LLLL|content|HH>
 * - LLLL: 4 hex ASCII chars for content length (e.g., "0FA0" = 4000)
 * - content: 0-4000 bytes of file data
 * - HH: CRC-8 as 2 hex ASCII chars
 *
 * EOF is signaled by zero-length content: <0000||HH>
 */
uint8_t file_admin_load_chunk(void) {
    if (!download_state.active) {
        return ADMIN_STATUS_INVALID_PACKET;
    }

    uint8_t *download_buffer = i2c_get_download_buffer();

    // Check if we've read everything (includes empty files: offset=0, size=0)
    if (download_state.offset >= download_state.file_size) {
        // Send EOF packet: <0000||HH>
        download_buffer[0] = PACKET_BEGIN;
        uint16_to_hex4(0, (char*)&download_buffer[1]);  // "0000"
        download_buffer[5] = PACKET_DELIMITER;
        download_buffer[6] = PACKET_DELIMITER;
        uint8_t crc = i2c_calculate_crc8(download_buffer, 7);  // CRC over <0000||
        byte_to_hex(crc, (char*)&download_buffer[7]);
        download_buffer[9] = PACKET_END;
        download_buffer[10] = '\0';
        i2c_set_download_buffer_index(0);

        // End session
        download_state.active = false;
        debug_log("Download complete\n");
        return ADMIN_STATUS_OK;
    }

    // Calculate chunk size
    uint32_t remaining = download_state.file_size - download_state.offset;
    uint32_t chunk_size = (remaining > ADMIN_MAX_FILE_CONTENT)
                          ? ADMIN_MAX_FILE_CONTENT
                          : remaining;

    // Open file and seek to position
    FIL file;
    if (f_open(&file, download_state.filepath, FA_READ) != FR_OK) {
        download_state.active = false;
        return ADMIN_STATUS_IO_ERROR;
    }

    if (download_state.offset > 0) {
        if (f_lseek(&file, download_state.offset) != FR_OK) {
            f_close(&file);
            download_state.active = false;
            return ADMIN_STATUS_IO_ERROR;
        }
    }

    // Read chunk into buffer at offset 6 (after header: <LLLL|)
    UINT bytes_read = 0;
    FRESULT res = f_read(&file, &download_buffer[6], chunk_size, &bytes_read);
    f_close(&file);

    if (res != FR_OK) {
        download_state.active = false;
        return ADMIN_STATUS_IO_ERROR;
    }

    // Build complete packet with actual bytes_read
    // Format: <LLLL|content|HH>
    download_buffer[0] = PACKET_BEGIN;
    uint16_to_hex4((uint16_t)bytes_read, (char*)&download_buffer[1]);
    download_buffer[5] = PACKET_DELIMITER;
    // Content is already at download_buffer[6..6+bytes_read-1]
    size_t content_end = 6 + bytes_read;
    download_buffer[content_end] = PACKET_DELIMITER;
    uint8_t crc = i2c_calculate_crc8(download_buffer, content_end + 1);
    byte_to_hex(crc, (char*)&download_buffer[content_end + 1]);
    download_buffer[content_end + 3] = PACKET_END;
    download_buffer[content_end + 4] = '\0';
    i2c_set_download_buffer_index(0);

    // Advance position
    download_state.offset += bytes_read;

    debug_log("Chunk: %lu bytes (offset now %lu/%lu)\n",
              (unsigned long)bytes_read, (unsigned long)download_state.offset,
              (unsigned long)download_state.file_size);

    return ADMIN_STATUS_OK;
}


uint8_t file_admin_upload(uint8_t dir) {
    // Only /schedule allowed for uploads
    if (dir != DIRECTORY_SCHEDULE) {
        debug_log("Upload rejected: only /schedule allowed\n");
        return ADMIN_STATUS_INVALID_DIRECTORY;
    }

    if (i2c_is_upload_buffer_overflowed()) {
        debug_log("Upload rejected: packet too large\n");
        return ADMIN_STATUS_FILE_TOO_LARGE;
    }

    // Get upload buffer via interface function
    const uint8_t *upload_buffer = i2c_get_upload_buffer();
    size_t buf_len = i2c_get_upload_buffer_len();
    if (!upload_buffer || buf_len == 0) {
        debug_log("Upload rejected: buffer is invalid\n");
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Parse packet: <filename|content|CRC>
    int start = find_byte_bounded(upload_buffer, buf_len, (uint8_t)PACKET_BEGIN, 0);
    if (start < 0) {
        debug_log("Upload rejected: missing PACKET_BEGIN\n");
        return ADMIN_STATUS_INVALID_PACKET;
    }
    int end = find_byte_bounded(upload_buffer, buf_len, (uint8_t)PACKET_END, (size_t)start + 1);
    if (end < 0 || end <= start + 1) {
        debug_log("Upload rejected: missing PACKET_END\n");
        return ADMIN_STATUS_INVALID_PACKET;
    }
    int delim1 = find_byte_bounded(upload_buffer, (size_t)end, (uint8_t)PACKET_DELIMITER, (size_t)start + 1);
    int delim2 = find_byte_bounded(upload_buffer, (size_t)end, (uint8_t)PACKET_DELIMITER, (size_t)delim1 + 1);
    if (delim1 < 0 || delim2 < 0 || delim1 <= start || delim2 <= delim1) {
        debug_log("Upload rejected: PACKET_DELIMITER is missing or misplaced\n");
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Extract filename
    int name_len = delim1 - start - 1;
    if (name_len <= 0 || name_len >= ADMIN_MAX_FILENAME_LEN) {
        debug_log("Upload rejected: filename length is %d\n", name_len);
        return ADMIN_STATUS_INVALID_PACKET;
    }
    char filename[ADMIN_MAX_FILENAME_LEN];
    memcpy(filename, &upload_buffer[start + 1], name_len);
    filename[name_len] = '\0';
    if (!is_allowed_schedule_filename(filename)) {
        debug_log("Upload rejected: unsupported filename extension: %s\n", filename);
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Extract content
    const char *content = &upload_buffer[delim1 + 1];
    int content_len = delim2 - delim1 - 1;
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
    // All directories allowed for download (1-4)
    if (dir < 1 || dir > 4) {
        return ADMIN_STATUS_INVALID_DIRECTORY;
    }

    // Extract filename from upload buffer
    uint8_t *upload_buffer = i2c_get_upload_buffer();
    char filename[ADMIN_MAX_FILENAME_LEN];
    if (!i2c_unpack_filename((char*)upload_buffer, filename)) {
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Build full filepath
    char filepath[ADMIN_MAX_FILEPATH_LEN];
    if (!build_filepath(dir, filename, filepath, sizeof(filepath))) {
        return ADMIN_STATUS_INVALID_PACKET;
    }

    // Get file info - NO LONGER REJECT LARGE FILES
    FILINFO fno;
    if (f_stat(filepath, &fno) != FR_OK) {
        return ADMIN_STATUS_FILE_NOT_FOUND;
    }

    // Ensure USB MSC is not mounted
    usb_msc_ensure_ejected();

    // Initialize download session
    strncpy(download_state.filepath, filepath, sizeof(download_state.filepath));
    download_state.filepath[sizeof(download_state.filepath) - 1] = '\0';
    download_state.file_size = fno.fsize;
    download_state.offset = 0;
    download_state.active = true;

    debug_log("Download started: %s (%lu bytes)\n", filepath, (unsigned long)fno.fsize);

    // Load first chunk (or EOF packet for empty files)
    return file_admin_load_chunk();
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
    if (!is_allowed_schedule_filename(filename)) {
        debug_log("Delete rejected: unsupported filename extension: %s\n", filename);
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
