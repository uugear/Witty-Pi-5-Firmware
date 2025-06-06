#include <time.h>
#include <hardware/powman.h>
#include <tusb.h>
#include <ff.h>
#include <diskio.h>

#include "fatfs_disk.h"
#include "flash.h"
#include "log.h"


typedef struct {
  uint8_t DIR_Name[11];
  uint8_t DIR_Attr;
  uint8_t DIR_NTRes;
  uint8_t DIR_CrtTimeTenth;
  uint16_t DIR_CrtTime;
  uint16_t DIR_CrtDate;
  uint16_t DIR_LstAccDate;
  uint16_t DIR_FstClusHI;
  uint16_t DIR_WrtTime;
  uint16_t DIR_WrtDate;
  uint16_t DIR_FstClusLO;
  uint32_t DIR_FileSize;
} fat_dir_entry_t;


static DSTATUS Stat = RES_OK;

FATFS filesystem;

bool fatfs_mounted = false;


DSTATUS disk_status(BYTE drv) {
    return Stat;
}


DSTATUS disk_initialize(BYTE drv) {
    Stat = RES_OK;
    return Stat;
}


DRESULT disk_read(BYTE drv, BYTE *buff, LBA_t sector, UINT count) {
    
    if (sector > FAT_BLOCK_NUM) {
        return RES_ERROR;
    }
    flash_fatfs_read(sector, (uint8_t *)buff, FAT_BLOCK_SIZE * count);
    return RES_OK;
}


DRESULT disk_write(BYTE drv, const BYTE *buff, LBA_t sector, UINT count) {
    
    tud_msc_start_stop_cb(0, 0, false, true);   // Eject USB MSC device first
    
    flash_fatfs_write(sector, (uint8_t *)buff, FAT_BLOCK_SIZE * count);
    
    return RES_OK;
}


DRESULT disk_ioctl(BYTE drv, BYTE ctrl, void *buff) {
    
    if (ctrl == GET_SECTOR_SIZE) {
        *(WORD*)buff = FAT_BLOCK_SIZE;
        return RES_OK;
    }
    return RES_OK;
}


DWORD get_fattime (void) {
    time_t time = powman_timer_get_ms() / 1000;
    struct tm *stm = localtime(&time);
    return (DWORD)(stm->tm_year - 80) << 25 |
           (DWORD)(stm->tm_mon + 1) << 21 |
           (DWORD)stm->tm_mday << 16 |
           (DWORD)stm->tm_hour << 11 |
           (DWORD)stm->tm_min << 5 |
           (DWORD)stm->tm_sec >> 1;
}


DWORD ff_wtoupper(DWORD chr) {
    if (chr >= 'a' && chr <= 'z') {
        chr -= 0x20;
    }
    return chr;
}


WCHAR ff_uni2oem(DWORD uni, WORD cp) {
    if (uni < 0x80) return (WCHAR)uni;
    return '?';
}


WCHAR ff_oem2uni(WCHAR oem, WORD cp) {
    if (oem < 0x80) return oem;
    return '?';
}


/**
 * Check if FatFs is mounted
 * 
 * @return true if FatFs is mounted, false otherwise
 */
bool is_fatfs_mounted(void) {
    return fatfs_mounted;
}


/**
 * Mount FatFs
 * 
 * @return true if mounted successfully, false otherwise
 */
bool mount_fatfs(void) {
    if (!fatfs_mounted) {
        if (f_mount(&filesystem, "/", 1) == FR_OK) {
            fatfs_mounted = true;
            return true;
        } else {
            printf("Mount filesystem failed\n");
            return false;
        }
    }
    return true;
}


/**
 * Unmount FatFs
 * 
 * @return true if unmounted successfully, false otherwise
 */
bool unmount_fatfs(void) {
    if (fatfs_mounted) {
        if (f_unmount("/") == FR_OK) {
            fatfs_mounted = false;
            return true;
        } else {
            printf("Unmount filesystem failed\n");
            return false;
        }
    }
    return true;
}


/**
 * Try to create a directory
 * 
 * @param path The path of the directory
 * @return true if the directory exists or gets created, false otherwise
 */
bool check_and_create_directory(const char *path) {
    FRESULT fr;
    DIR dir;
    fr = f_opendir(&dir, path);
    if (fr == FR_OK) {
        f_closedir(&dir);
        return true;
        
    } else if (fr == FR_NO_PATH) {
        fr = f_mkdir(path);
        return fr == FR_OK;
    }
    return false;
}


/**
 * Create default directories
 */
void create_default_dirs(void) {
    
	if (!check_and_create_directory("/log")) {
        debug_log("Error creating log directory\n");
    }
    
	if (!check_and_create_directory("/conf")) {
        debug_log("Error creating conf directory\n");
    }

	if (!check_and_create_directory("/schedule")) {
        debug_log("Error creating schedule directory\n");
    }
}


/**
 * Check if a file exists
 * 
 * @param path The path of the file
 * @return true if the file exists, false otherwise
 */
bool file_exists(const char *path) {
    FILINFO fno;
    FRESULT res;
    res = f_stat(path, &fno);
    return res == FR_OK;
}


/**
 * Delete a file if it exists
 * 
 * @param path The path of the file
 * @return true if deleted, false otherwise
 */
bool file_delete(const char *path) {
    FRESULT fr;
    if (path == NULL) {
        return false;
    }
    fr = f_unlink(path);
    if (fr == FR_OK) {
        return true;
    } else {
        return false;
    }
}


/**
 * Copies a file from source to destination using FatFs
 *
 * @param dest Destination file path
 * @param src Source file path
 * @return true if copy was successful, false otherwise
 */
bool file_copy(const char *dest, const char *src) {
    FIL fsrc, fdst;
    FRESULT fr;
    UINT br, bw;
    BYTE buffer[512];
    bool result = true;

    fr = f_open(&fsrc, src, FA_READ);
    if (fr != FR_OK) {
        debug_log("Error: Cannot open source file (%s), error code: %d\n", src, fr);
        return false;
    }

    fr = f_open(&fdst, dest, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        debug_log("Error: Cannot create destination file (%s), error code: %d\n", dest, fr);
        f_close(&fsrc);
        return false;
    }

    for (;;) {
        fr = f_read(&fsrc, buffer, sizeof(buffer), &br);
        if (fr != FR_OK || br == 0) break;
        
        fr = f_write(&fdst, buffer, br, &bw);
        if (fr != FR_OK || bw < br) {
            debug_log("Error: Write error, error code: %d\n", fr);
            result = false;
            break;
        }
    }

    f_close(&fsrc);
    f_close(&fdst);
    if (fr != FR_OK && fr != FR_INT_ERR && result) {
        debug_log("Error: Read error, error code: %d\n", fr);
        result = false;
    }
    return result;
}


/**
 * Load content of given file
 *
 * @param path The path for given file
 * @param buffer The buffer to store the loaded content
 * @param buf_size The size of buffer
 * @return the number of bytes loaded from the file, -1 for failure
 */
int load_file(const char *path, char *buffer, int buf_size) {
    FIL file;
    FRESULT fr;
    UINT bytes_read;
    int total_read = 0;

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        debug_log("Error: Cannot open file (%s), error code: %d\n", path, fr);
        return -1;
    }

    int file_size = f_size(&file);
    if (file_size > buf_size) {
        debug_log("Error: The size of file (%s) exceeds buffer size: %d\n", path, file_size);
        return -1;
    }

    fr = f_read(&file, buffer, file_size, &bytes_read);
    if (fr != FR_OK) {
        debug_log("Error: Failed to read file (%s), error code: %d\n", path, fr);
        f_close(&file);
        return -1;
    }

    total_read = bytes_read;
    
    f_close(&file);

    return total_read;
}


/**
 * Read a line from given file
 *
 * @param buff The buffer to store the line
 * @param len The size of buffer
 * @param file The pointer to FIL object
 * @return buff will be returuned when success, NULL otherwise
 */
char * f_read_line(char* buff, int len, FIL* file) {
    UINT br;
    int i = 0;
    char c;
    while (i < len - 1) {
        FRESULT res = f_read(file, &c, 1, &br);
        if (res != FR_OK || br == 0) break;
        
        buff[i++] = c;
        
        if (c == '\n') break;
    }
    buff[i] = '\0';
    return (i > 0) ? buff : NULL;
}