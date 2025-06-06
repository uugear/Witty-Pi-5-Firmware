#ifndef _FATFS_DISK_H_
#define _FATFS_DISK_H_

#include <stdbool.h>
#include <ff.h>

/**
 * Check if FatFs is mounted
 * 
 * @return true if FatFs is mounted, false otherwise
 */
bool is_fatfs_mounted(void);


/**
 * Mount FatFs
 * 
 * @return true if mounted successfully, false otherwise
 */
bool mount_fatfs(void);


/**
 * Unmount FatFs
 * 
 * @return true if unmounted successfully, false otherwise
 */
bool unmount_fatfs(void);


/**
 * Try to create a directory
 * 
 * @param path The path of the directory
 * @return true if the directory exists or gets created, false otherwise
 */
bool check_and_create_directory(const char *path);


/**
 * Create default directories
 */
void create_default_dirs(void);


/**
 * Check if a file exists
 * 
 * @param path The path of the file
 * @return true if the file exists, false otherwise
 */
bool file_exists(const char *path);


/**
 * Delete a file if it exists
 * 
 * @param path The path of the file
 * @return true if deleted, false otherwise
 */
bool file_delete(const char *path);


/**
 * Copies a file from source to destination using FatFs
 *
 * @param dest Destination file path
 * @param src Source file path
 * @return true if copy was successful, false otherwise
 */
bool file_copy(const char *dest, const char *src);


/**
 * Load content of give file
 *
 * @param path The path for given file
 * @param buffer The buffer to store the loaded content
 * @param buf_size The size of buffer
 * @return the number of bytes loaded from the file, -1 for failure
 */
int load_file(const char *path, char *buffer, int buf_size);


/**
 * Read a line from given file
 *
 * @param buff The buffer to store the line
 * @param len The size of buffer
 * @param file The pointer to FIL object
 * @return FR_OK if read successfully, FR_INT_ERR for failure
 */
char * f_read_line(char* buff, int len, FIL* file);

#endif