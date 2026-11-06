#ifndef WP5_DST_TIME_H
#define WP5_DST_TIME_H

#include <stdbool.h>
#include <stdint.h>

/* A transition uses the wall clock in effect immediately BEFORE the change. */
typedef struct {
    uint8_t month;
    uint8_t week;       /* 0=last, 1..5=nth; ignored for fixed-date mode */
    uint8_t weekday;    /* 0=Sunday .. 6=Saturday */
    uint8_t date;       /* 1..31 for fixed-date mode */
    uint8_t hour;
    uint8_t minute;
} DstTimeSpec;

typedef struct {
    uint8_t offset_minutes; /* 0 means disabled */
    bool fixed_date;
    DstTimeSpec begin;
    DstTimeSpec end;
} DstRule;

bool dst_rule_valid(const DstRule *rule);
int dst_year_from_seconds(int64_t timestamp);
int64_t dst_calendar_seconds(int year, int month, int day, int hour, int minute, int second);
bool dst_transition_seconds(const DstRule *rule, int year, bool begin, int64_t *standard_ts);
bool dst_is_active_standard(const DstRule *rule, int64_t standard_ts);
/* A nonexistent spring local time is shifted forward; an ambiguous autumn time picks first occurrence. */
int64_t dst_local_to_standard(const DstRule *rule, int64_t local_ts);
int64_t dst_standard_to_local(const DstRule *rule, int64_t standard_ts);
/* Keep an ordered script sequence monotonic when spring remapping reverses actions. */
int64_t dst_resolve_ordered_action(const DstRule *rule, int64_t nominal,
                                   int64_t previous_nominal,
                                   int64_t previous_standard);
bool dst_next_transition(const DstRule *rule, int64_t standard_now,
                         int64_t *next_standard_ts, bool *entering_dst);

#endif
