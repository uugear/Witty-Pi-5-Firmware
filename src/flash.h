#ifndef _FLASH_H_
#define _FLASH_H_

#include <ctype.h>
#include <math.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>


// 1 FAT SECTOR = 1 FAT BLOCK = 512 Bytes
// 1 FAT CLUSTER = 8 FAT SECTORS = 4096 Bytes

#define FLASH_FAT_BLOCK_SIZE   4096
#define FLASH_FAT_OFFSET       0x1F0000

#define FAT_BLOCK_NUM          28672  // 14MB
#define FAT_BLOCK_SIZE         512


/**
 * Write 4k sector into flash
 * 
 * @param offset The sector offset
 * @param b0 Pointer to block0
 * @param b1 Pointer to block1
 * @param b2 Pointer to block2
 * @param b3 Pointer to block3
 * @param b4 Pointer to block4
 * @param b5 Pointer to block5
 * @param b6 Pointer to block6
 * @param b7 Pointer to block7
 * @return number of written bytes
 */
int flash_write_4k_sector(int offset, uint8_t *b0, uint8_t *b1, uint8_t *b2, uint8_t *b3, uint8_t *b4, uint8_t *b5, uint8_t *b6, uint8_t *b7);

/**
 * Initialize FatFS for flash
 */
void flash_fatfs_init(void);


/**
 * Read specific block into buffer
 * 
 * @param block The block offset
 * @param buffer Pointer to buffer
 * @param buffer_size Size of buffer
 * @return true if read succeed
 */
bool flash_fatfs_read(int block, uint8_t *buffer, size_t buffer_size);


/**
 * Write data in buffer to specific block
 * 
 * @param block The block offset
 * @param buffer Pointer to buffer
 * @param buffer_size Size of buffer
 * @return true if write succeed
 */
bool flash_fatfs_write(int block, uint8_t *buffer, size_t buffer_size);


#endif
