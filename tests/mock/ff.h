#ifndef MOCK_FF_H
#define MOCK_FF_H
#include <stdint.h>
typedef unsigned int UINT;
typedef int FRESULT;
typedef struct { unsigned position; } FIL;
typedef struct { uint32_t fsize; uint16_t fdate; uint16_t ftime; } FILINFO;
FRESULT f_stat(const char *path,FILINFO *info);
#define FR_OK 0
#define FR_INT_ERR 2
#define FA_READ 1
#define FA_WRITE 2
#define FA_CREATE_ALWAYS 4
FRESULT f_open(FIL *file, const char *path, int mode);
FRESULT f_close(FIL *file);
FRESULT f_read(FIL *file, void *dst, UINT len, UINT *read);
FRESULT f_write(FIL *file, const void *src, UINT len, UINT *written);
#endif
