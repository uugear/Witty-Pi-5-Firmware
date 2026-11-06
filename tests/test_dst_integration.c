#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "dst.h"
#include "conf.h"
#include "rtc.h"
#include "main.h"

#define T(y,m,d,h,n,s) dst_calendar_seconds(y,m,d,h,n,s)
static int64_t rtc_clock;
static bool fail_rtc_write;
static int rtc_writes;
static int config_saves;
static int callbacks;
static uint8_t settings[10];
static uint8_t alarm_day, alarm_hour, alarm_minute, alarm_second;
static uint8_t shutdown_day, shutdown_hour, shutdown_minute, shutdown_second;
uint8_t current_rpi_state = STATE_OFF;

uint8_t conf_get(const char *key) {
    if (!strcmp(key,CONF_ALARM1_DAY)) return alarm_day;
    if (!strcmp(key,CONF_ALARM1_HOUR)) return alarm_hour;
    if (!strcmp(key,CONF_ALARM1_MINUTE)) return alarm_minute;
    if (!strcmp(key,CONF_ALARM1_SECOND)) return alarm_second;
    if (!strcmp(key,CONF_ALARM2_DAY)) return shutdown_day;
    if (!strcmp(key,CONF_ALARM2_HOUR)) return shutdown_hour;
    if (!strcmp(key,CONF_ALARM2_MINUTE)) return shutdown_minute;
    if (!strcmp(key,CONF_ALARM2_SECOND)) return shutdown_second;
    static const char *const keys[] = {
        CONF_DST_OFFSET, CONF_DST_BEGIN_MON, CONF_DST_BEGIN_DAY,
        CONF_DST_BEGIN_HOUR, CONF_DST_BEGIN_MIN, CONF_DST_END_MON,
        CONF_DST_END_DAY, CONF_DST_END_HOUR, CONF_DST_END_MIN, CONF_DST_APPLIED
    };
    for (int i=0;i<10;i++) if (!strcmp(key,keys[i])) return settings[i];
    return 0;
}
bool conf_set(const char *key,uint8_t value) {
    if (!strcmp(key,CONF_DST_APPLIED)) { settings[9]=value; return true; }
    return false;
}
void conf_sync(void) { ++config_saves; }
bool register_item_changed_callback(const char *key, item_changed_callback_t callback) {
    (void)key; (void)callback; callbacks++; return true;
}
int64_t rtc_get_timestamp(bool *valid) { *valid = true; return rtc_clock; }
bool rtc_set_timestamp(int64_t ts) {
    ++rtc_writes;
    if (fail_rtc_write) return false;
    rtc_clock=ts; return true;
}
int get_days_in_month(int year,int month) {
    static const int md[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
    return md[month] + (month==2 && year%4==0 && (year%100!=0 || year%400==0));
}
void timestamp_to_datetime(int64_t ts, DateTime *dt) {
    int year=dst_year_from_seconds(ts);
    int month=1;
    while (month<12 && ts>=dst_calendar_seconds(year,month+1,1,0,0,0)) ++month;
    int64_t remaining=ts-dst_calendar_seconds(year,month,1,0,0,0);
    memset(dt,0,sizeof(*dt));
    dt->year=year;dt->month=month;dt->day=1+remaining/86400;
    remaining%=86400;
    dt->hour=remaining/3600;remaining%=3600;
    dt->min=remaining/60;dt->sec=remaining%60;
}
void debug_log(const char *fmt, ...) { (void)fmt; }

static void config_europe(void) {
    settings[0]=60;settings[1]=0x03;settings[2]=0x00;
    settings[3]=0x02;settings[4]=0x00;
    settings[5]=0x10;settings[6]=0x00;
    settings[7]=0x03;settings[8]=0x00;settings[9]=0;
}

int main(void) {
    config_europe();
    dst_register_config_callbacks();
    assert(callbacks==10);
    rtc_clock=T(2027,3,28,1,59,57);
    uint64_t delay;
    assert(dst_get_next_wakeup_ms(&delay) && delay==3000);
    assert(!dst_process_due_transition());
    rtc_clock=T(2027,3,28,2,0,0);
    assert(dst_process_due_transition());
    assert(rtc_clock==T(2027,3,28,3,0,0) && settings[9]==1);
    assert(config_saves==1 && rtc_writes==1);
    assert(!dst_process_due_transition());
    alarm_day=0x28;alarm_hour=0x02;alarm_minute=0x30;alarm_second=0;
    assert(!dst_configured_alarm_ready(false));
    rtc_clock=T(2027,3,28,3,30,0);
    assert(dst_configured_alarm_ready(false));
    rtc_clock=T(2027,3,28,3,0,0);
    alarm_hour=0x03;alarm_minute=0;
    assert(dst_configured_alarm_ready(true));
    assert(dst_action_to_standard(T(2027,3,28,2,30,0))==T(2027,3,28,2,30,0));
    assert(dst_action_to_hardware_local(T(2027,3,28,2,30,0))==T(2027,3,28,3,30,0));

    rtc_clock=T(2027,10,31,2,59,57);settings[9]=1;
    assert(dst_get_next_wakeup_ms(&delay) && delay==3000);
    rtc_clock=T(2027,10,31,3,0,0);
    assert(dst_process_due_transition());
    assert(rtc_clock==T(2027,10,31,2,0,0) && settings[9]==0);
    rtc_clock=T(2027,10,31,2,15,0);
    assert(!dst_process_due_transition());
    alarm_day=0x31;alarm_hour=0x02;alarm_minute=0x30;
    rtc_clock=T(2027,10,31,2,30,0);
    assert(!dst_configured_alarm_ready(false));
    rtc_clock=T(2027,10,31,2,15,0);
    assert(dst_action_to_standard(T(2027,10,31,2,30,0)) <
           T(2027,10,31,2,15,0));

    config_europe();
    rtc_clock=T(2027,3,28,2,0,0);
    fail_rtc_write=true;
    assert(!dst_process_due_transition());
    assert(settings[9]==0 && rtc_clock==T(2027,3,28,2,0,0));
    fail_rtc_write=false;
    settings[1]=0x99;
    DstRule disabled;
    assert(dst_get_rule(&disabled) && disabled.offset_minutes==0);
    assert(dst_action_to_standard(T(2027,3,28,2,30,0)) == T(2027,3,28,2,30,0));
    config_europe();

    /* A regular pulse that wakes AFTER a missed DST deadline must request
     * a dedicated recovery wake, not silently wait until October. */
    rtc_clock=T(2027,3,28,2,0,10);
    assert(dst_get_next_wakeup_ms(&delay) && delay==0);
    assert(dst_process_due_transition());
    assert(rtc_clock==T(2027,3,28,3,0,10) && settings[9]==1);

    /* Regression: a 03:06 shutdown is NOT due at 03:05:05. */
    current_rpi_state=STATE_STARTING;
    shutdown_day=0x28;
    shutdown_hour=0x03;
    shutdown_minute=0x06;
    shutdown_second=0x00;
    rtc_clock=T(2027,3,28,3,5,5);
    assert(!dst_configured_alarm_ready(true));
    rtc_clock=T(2027,3,28,3,5,59);
    assert(!dst_configured_alarm_ready(true));
    rtc_clock=T(2027,3,28,3,6,0);
    assert(dst_configured_alarm_ready(false));
    rtc_clock=T(2027,3,28,3,6,2);
    assert(dst_configured_alarm_ready(false));
    rtc_clock=T(2027,3,28,3,7,0);
    assert(!dst_configured_alarm_ready(true));
    current_rpi_state=STATE_OFF;

    /* Autumn fallback also needs catch-up after the intended deadline. */
    rtc_clock=T(2027,10,31,3,0,10);
    settings[9]=1;
    assert(dst_get_next_wakeup_ms(&delay) && delay==0);
    assert(dst_process_due_transition());
    assert(rtc_clock==T(2027,10,31,2,0,10) && settings[9]==0);

    puts("PASS: DST transitions, overdue wake recovery, exact-minute alarm dispatch");
    return 0;
}
