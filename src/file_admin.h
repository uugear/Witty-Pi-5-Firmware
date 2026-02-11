#ifndef _FILE_ADMIN_H_
#define _FILE_ADMIN_H_

#include <stdint.h>
#include <stdbool.h>

// Admin status codes for I2C_ADMIN_CONTEXT register
#define ADMIN_STATUS_OK                     0x00
#define ADMIN_STATUS_FILE_NOT_FOUND         0x01
#define ADMIN_STATUS_CANNOT_DELETE_ACTIVE   0x02
#define ADMIN_STATUS_IO_ERROR               0x03
#define ADMIN_STATUS_INVALID_PACKET         0x04
#define ADMIN_STATUS_FILE_TOO_LARGE         0x05
#define ADMIN_STATUS_INVALID_DIRECTORY      0x06

/*
 * File operation limits
 *
 * ADMIN_MAX_FILE_CONTENT: Maximum file content size in bytes.
 * This is set to 4000 bytes (arbitrary, derived from 4096 buffer minus packet overhead).
 *
 * Capacity estimate for .wpi schedule files:
 *   - Header (BEGIN + END lines): ~50 bytes
 *   - Remaining for ON/OFF lines: ~3950 bytes
 *   - Average ON/OFF line length: ~10 bytes (e.g., "ON H2M30\n")
 *   - Maximum schedule lines: ~395 ON/OFF lines (~200 ON/OFF cycles)
 *
 * Note: The .wpi parser (script.c) independently limits to 128 lines (WPI_MAX_LINES),
 * so in practice ~128 schedule lines is the effective limit regardless of buffer size.
 */
#define ADMIN_MAX_FILE_CONTENT              4000
#define ADMIN_MAX_FILENAME_LEN              48
#define ADMIN_MAX_FILEPATH_LEN              64

/**
 * Handle FILE_UPLOAD command
 * @param dir Directory index from I2C_ADMIN_DIR register
 * @return Status code for I2C_ADMIN_CONTEXT
 */
uint8_t file_admin_upload(uint8_t dir);

/**
 * Handle FILE_DOWNLOAD command
 * @param dir Directory index from I2C_ADMIN_DIR register
 * @return Status code for I2C_ADMIN_CONTEXT
 */
uint8_t file_admin_download(uint8_t dir);

/**
 * Handle FILE_DELETE command
 * @param dir Directory index from I2C_ADMIN_DIR register
 * @return Status code for I2C_ADMIN_CONTEXT
 */
uint8_t file_admin_delete(uint8_t dir);

#endif
