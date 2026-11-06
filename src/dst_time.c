#include "dst_time.h"

static bool leap(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int month_days(int year, int month) {
    static const int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    return days[month] + (month == 2 && leap(year));
}

int64_t dst_calendar_seconds(int year, int month, int day, int hour, int minute, int second) {
    int64_t days = 0;
    for (int y = 2000; y < year; ++y) days += leap(y) ? 366 : 365;
    for (int m = 1; m < month; ++m) days += month_days(year, m);
    days += day - 1;
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

int dst_year_from_seconds(int64_t ts) {
    if (ts < 0) return -1;
    int year = 2000;
    while (year <= 2099) {
        int64_t length = (leap(year) ? 366LL : 365LL) * 86400;
        if (ts < length) return year;
        ts -= length;
        ++year;
    }
    return -1;
}

static int weekday(int year, int month, int day) {
    int64_t days = dst_calendar_seconds(year, month, day, 0, 0, 0) / 86400;
    return (int)((days + 6) % 7); /* 2000-01-01 was Saturday. */
}

static bool valid_spec(const DstRule *rule, const DstTimeSpec *spec) {
    if (spec->month < 1 || spec->month > 12 || spec->hour > 23 || spec->minute > 59)
        return false;
    if (rule->fixed_date)
        return spec->date > 0 && spec->date <= month_days(2023, spec->month);
    return spec->week <= 5 && spec->weekday <= 6;
}

bool dst_rule_valid(const DstRule *rule) {
    return rule && rule->offset_minutes <= 127 &&
        (!rule->offset_minutes ||
         (valid_spec(rule, &rule->begin) && valid_spec(rule, &rule->end)));
}

static bool wall_transition(const DstRule *rule, int year, bool begin,
                            int64_t *wall_ts) {
    if (!dst_rule_valid(rule) || !rule->offset_minutes || year < 2000 || year > 2099)
        return false;
    const DstTimeSpec *spec = begin ? &rule->begin : &rule->end;
    const int days = month_days(year, spec->month);
    int date;
    if (rule->fixed_date) {
        date = spec->date;
        if (date > days) return false;
    } else if (spec->week == 0) {
        date = days - (weekday(year, spec->month, days) - spec->weekday + 7) % 7;
    } else {
        date = 1 + (spec->weekday - weekday(year, spec->month, 1) + 7) % 7 +
               (spec->week - 1) * 7;
        if (date > days) return false;
    }
    *wall_ts = dst_calendar_seconds(year, spec->month, date, spec->hour,
                                    spec->minute, 0);
    return true;
}

bool dst_transition_seconds(const DstRule *rule, int year, bool begin,
                            int64_t *standard_ts) {
    if (!standard_ts) return false;
    int64_t wall;
    if (!wall_transition(rule, year, begin, &wall)) return false;
    *standard_ts = wall - (begin ? 0 : (int64_t)rule->offset_minutes * 60);
    return true;
}

bool dst_is_active_standard(const DstRule *rule, int64_t std_ts) {
    if (!dst_rule_valid(rule) || !rule->offset_minutes) return false;
    int year = dst_year_from_seconds(std_ts);
    if (year < 0) return false;
    int64_t last = INT64_MIN;
    bool active = false;
    for (int y = year - 1; y <= year + 1; ++y) {
        for (int k = 0; k < 2; ++k) {
            int64_t transition;
            bool begins = (k == 0);
            if (dst_transition_seconds(rule, y, begins, &transition) &&
                transition <= std_ts && transition >= last) {
                last = transition;
                active = begins;
            }
        }
    }
    return active;
}

int64_t dst_local_to_standard(const DstRule *rule, int64_t local_ts) {
    if (!dst_rule_valid(rule) || !rule->offset_minutes) return local_ts;
    const int year = dst_year_from_seconds(local_ts);
    if (year < 0) return local_ts;
    int64_t begin, end;
    if (!wall_transition(rule, year, true, &begin) ||
        !wall_transition(rule, year, false, &end)) return local_ts;
    const int64_t offset = (int64_t)rule->offset_minutes * 60;
    /* The spring gap is interpreted on the old standard time axis. */
    if (local_ts >= begin && local_ts < begin + offset) return local_ts;
    bool summer = begin < end
        ? (local_ts >= begin + offset && local_ts < end)
        : (local_ts >= begin + offset || local_ts < end);
    return local_ts - (summer ? offset : 0);
}

int64_t dst_standard_to_local(const DstRule *rule, int64_t standard_ts) {
    return standard_ts + (dst_is_active_standard(rule, standard_ts)
        ? (int64_t)rule->offset_minutes * 60 : 0);
}

int64_t dst_resolve_ordered_action(const DstRule *rule, int64_t nominal,
                                   int64_t previous_nominal,
                                   int64_t previous_standard) {
    int64_t resolved = dst_local_to_standard(rule, nominal);
    if (previous_nominal >= 0 && nominal > previous_nominal &&
        resolved <= previous_standard) {
        /* Keep the script's original elapsed spacing for the overlapping
         * ON/OFF sequence; do not let a following shutdown precede startup. */
        resolved = previous_standard + (nominal - previous_nominal);
    }
    return resolved;
}

bool dst_next_transition(const DstRule *rule, int64_t standard_now,
                         int64_t *next_standard_ts, bool *entering_dst) {
    if (!dst_rule_valid(rule) || !rule->offset_minutes ||
        !next_standard_ts || !entering_dst) return false;
    int year = dst_year_from_seconds(standard_now);
    if (year < 0) return false;
    bool found = false;
    int64_t earliest = INT64_MAX;
    bool enters = false;
    for (int y = year - 1; y <= year + 1; ++y) {
        for (int k = 0; k < 2; ++k) {
            int64_t ts;
            bool begin = (k == 0);
            if (dst_transition_seconds(rule, y, begin, &ts) &&
                ts > standard_now && ts < earliest) {
                earliest = ts;
                enters = begin;
                found = true;
            }
        }
    }
    if (found) {
        *next_standard_ts = earliest;
        *entering_dst = enters;
    }
    return found;
}
