#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "dst_time.h"
#include "dst.h"
#include "script.h"
#include "ff.h"

#define T(y,m,d,h,n,s) dst_calendar_seconds(y,m,d,h,n,s)

static char lines[8][48];
static int line_count;
static int64_t current_standard;

FRESULT f_open(FIL *f, const char *path, int mode) {
    (void)path; (void)mode; f->position=0; return FR_OK;
}
FRESULT f_close(FIL *f) { (void)f; return FR_OK; }
FRESULT f_read(FIL *f,void *dst,UINT len,UINT *read) {
    (void)f;(void)dst;(void)len; *read=0; return FR_OK;
}
FRESULT f_write(FIL *f,const void *src,UINT len,UINT *written) {
    (void)f;(void)src;*written=len;return FR_OK;
}
char *f_read_line(char *out,int cap,FIL *f) {
    if ((int)f->position>=line_count) return NULL;
    snprintf(out,(size_t)cap,"%s",lines[f->position++]);
    return out;
}
bool dst_get_rule(DstRule *r) {
    *r=(DstRule){0};
    r->offset_minutes=60;
    r->begin=(DstTimeSpec){.month=3,.week=0,.weekday=0,.hour=2};
    r->end=(DstTimeSpec){.month=10,.week=0,.weekday=0,.hour=3};
    return true;
}
bool dst_get_standard_now(int64_t *ts) { *ts=current_standard;return true; }
static void add(char kind,int64_t time) {
    assert(line_count<8);
    snprintf(lines[line_count++],sizeof(lines[0]),"%c%lld\n",kind,(long long)time);
}

int main(void) {
    Action startup,shutdown;
    add('U',T(2027,3,28,2,30,0));
    add('D',T(2027,3,28,3,0,0));
    add('U',T(2027,3,29,2,30,0));
    add('D',T(2027,3,29,3,0,0));
    current_standard=T(2027,3,28,1,55,0);
    assert(find_next_actions_from_skd("test",0,true,&startup,&shutdown));
    assert((int64_t)startup.time==T(2027,3,28,3,30,0));
    assert((int64_t)shutdown.time==T(2027,3,28,4,0,0));
    current_standard=T(2027,3,28,2,40,0);
    assert(find_next_actions_from_skd("test",0,false,&startup,&shutdown));
    assert((int64_t)shutdown.time==T(2027,3,28,4,0,0));
    assert((int64_t)startup.time==T(2027,3,29,2,30,0));

    line_count=0;
    add('U',T(2027,10,31,2,30,0));
    add('D',T(2027,10,31,3,30,0));
    add('U',T(2027,11,1,2,30,0));
    add('D',T(2027,11,1,3,30,0));
    current_standard=T(2027,10,31,1,29,0);
    assert(find_next_actions_from_skd("test",0,true,&startup,&shutdown));
    assert((int64_t)startup.time==T(2027,10,31,2,30,0));
    current_standard=T(2027,10,31,2,10,0);
    assert(find_next_actions_from_skd("test",0,true,&startup,&shutdown));
    assert((int64_t)startup.time==T(2027,11,1,2,30,0));
    assert((int64_t)shutdown.time==T(2027,11,1,3,30,0));

    puts("PASS: actual .skd scanner spring sequence and autumn non-replay");
    return 0;
}
