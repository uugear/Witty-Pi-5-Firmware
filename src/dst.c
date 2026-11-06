#include <stdint.h>
#include <pico/time.h>

#include "dst.h"
#include "conf.h"
#include "rtc.h"
#include "util.h"
#include "main.h"
#include "log.h"

#define DST_ACTIVE_CHECK_MAX_SECONDS (6 * 3600)

static alarm_id_t active_check_alarm_id = -1;
static volatile bool active_check_pending = false;

static int64_t active_check_callback(alarm_id_t id, void *data) {
    (void)id;
    (void)data;
    active_check_pending = true;
    return 0;
}

static bool valid_bcd(uint8_t value) {
    return (value & 0x0f) <= 9 && ((value >> 4) & 0x0f) <= 9;
}

bool dst_get_rule(DstRule *rule) {
    if (!rule) return false;
    static bool invalid_reported = false;
    uint8_t encoded = conf_get(CONF_DST_OFFSET);
    rule->offset_minutes = encoded & 0x7f;
    rule->fixed_date = (encoded & 0x80) != 0;
    uint8_t begin_day = bcd_to_dec(conf_get(CONF_DST_BEGIN_DAY));
    uint8_t end_day = bcd_to_dec(conf_get(CONF_DST_END_DAY));
    rule->begin = (DstTimeSpec){
        .month = bcd_to_dec(conf_get(CONF_DST_BEGIN_MON)),
        .week = begin_day / 10, .weekday = begin_day % 10,
        .date = begin_day,
        .hour = bcd_to_dec(conf_get(CONF_DST_BEGIN_HOUR)),
        .minute = bcd_to_dec(conf_get(CONF_DST_BEGIN_MIN))
    };
    rule->end = (DstTimeSpec){
        .month = bcd_to_dec(conf_get(CONF_DST_END_MON)),
        .week = end_day / 10, .weekday = end_day % 10,
        .date = end_day,
        .hour = bcd_to_dec(conf_get(CONF_DST_END_HOUR)),
        .minute = bcd_to_dec(conf_get(CONF_DST_END_MIN))
    };
    const char *const bcd_keys[] = {
        CONF_DST_BEGIN_MON, CONF_DST_BEGIN_DAY, CONF_DST_BEGIN_HOUR,
        CONF_DST_BEGIN_MIN, CONF_DST_END_MON, CONF_DST_END_DAY,
        CONF_DST_END_HOUR, CONF_DST_END_MIN
    };
    bool valid = dst_rule_valid(rule);
    if (rule->offset_minutes != 0) {
        for (unsigned i = 0; i < sizeof(bcd_keys) / sizeof(bcd_keys[0]); ++i) {
            if (!valid_bcd(conf_get(bcd_keys[i]))) valid = false;
        }
    }
    if (!valid) {
        if (!invalid_reported) {
            debug_log("DST: invalid configuration; ignoring DST until corrected.\n");
            invalid_reported = true;
        }
        /* Never disable ordinary alarms because a DST rule is invalid. */
        rule->offset_minutes = 0;
    } else {
        invalid_reported = false;
    }
    return true;
}

bool dst_get_standard_now(int64_t *standard_ts) {
    if (!standard_ts) return false;
    bool valid;
    int64_t local_ts = rtc_get_timestamp(&valid);
    if (!valid) return false;
    DstRule rule;
    if (!dst_get_rule(&rule)) return false;
    *standard_ts = local_ts -
        ((rule.offset_minutes && conf_get(CONF_DST_APPLIED))
         ? (int64_t)rule.offset_minutes * 60 : 0);
    return true;
}

int64_t dst_action_to_standard(int64_t local_action_ts) {
    DstRule rule;
    if (!dst_get_rule(&rule)) return local_action_ts;
    return dst_local_to_standard(&rule, local_action_ts);
}

int64_t dst_action_to_hardware_local(int64_t local_action_ts) {
    DstRule rule;
    if (!dst_get_rule(&rule)) return local_action_ts;
    return dst_standard_to_local(&rule, dst_local_to_standard(&rule, local_action_ts));
}

bool dst_get_next_wakeup_ms(uint64_t *delay_ms) {
    DstRule rule;
    int64_t now, next;
    bool entering;
    if (!delay_ms || !dst_get_rule(&rule) || !rule.offset_minutes ||
        !dst_get_standard_now(&now)) return false;

    /* A missed transition must not disappear when the next transition is
     * months away. Schedule a dedicated recovery wake instead of silently
     * continuing regular pulses with an incorrect local RTC. */
    if (dst_is_active_standard(&rule, now) !=
        (conf_get(CONF_DST_APPLIED) != 0)) {
        *delay_ms = 0;
        return true;
    }

    if (!dst_next_transition(&rule, now, &next, &entering)) return false;
    if (next <= now) return false;
    *delay_ms = (uint64_t)(next - now) * 1000;
    return true;
}

bool dst_process_due_transition(void) {
    DstRule rule;
    int64_t now_std;
    if (!dst_get_rule(&rule) || !rule.offset_minutes ||
        !dst_get_standard_now(&now_std)) return false;
    bool applied = conf_get(CONF_DST_APPLIED) != 0;
    bool required = dst_is_active_standard(&rule, now_std);
    if (applied == required) return false;

    bool valid;
    int64_t old_local = rtc_get_timestamp(&valid);
    if (!valid) return false;
    int64_t change = (int64_t)rule.offset_minutes * 60 * (required ? 1 : -1);
    if (!rtc_set_timestamp(old_local + change)) {
        debug_log("DST: RTC update failed; state unchanged.\n");
        return false;
    }
    conf_set(CONF_DST_APPLIED, required ? 1 : 0);
    conf_sync();
    debug_log("DST: %s, RTC adjusted by %lld seconds.\n",
              required ? "enter" : "leave", (long long)change);
    return true;
}

bool dst_configured_alarm_ready(bool allow_overdue) {
    DstRule rule;
    int64_t std_now;
    if (!dst_get_rule(&rule) || !dst_get_standard_now(&std_now)) return false;
    bool startup = current_rpi_state == STATE_OFF || current_rpi_state == STATE_STOPPING;
    int sec = bcd_to_dec(conf_get(startup ? CONF_ALARM1_SECOND : CONF_ALARM2_SECOND));
    int min = bcd_to_dec(conf_get(startup ? CONF_ALARM1_MINUTE : CONF_ALARM2_MINUTE));
    int hour = bcd_to_dec(conf_get(startup ? CONF_ALARM1_HOUR : CONF_ALARM2_HOUR));
    int day = bcd_to_dec(conf_get(startup ? CONF_ALARM1_DAY : CONF_ALARM2_DAY));
    if (day < 1 || day > 31 || hour > 23 || min > 59 || sec > 59) return false;

    bool valid;
    int64_t local_now = rtc_get_timestamp(&valid);
    if (!valid) return false;
    DateTime date;
    timestamp_to_datetime(local_now, &date);
    if (day > get_days_in_month(date.year, date.month)) return false;
    int64_t nominal = dst_calendar_seconds(date.year, date.month, day, hour, min, sec);
    int64_t due = dst_local_to_standard(&rule, nominal);
    /* The RX8025 compares calendar minutes. Comparing a 0..59 second
     * window instead would incorrectly fire an action in the PREVIOUS
     * minute (e.g. 03:05:05 for a 03:06:00 shutdown). */
    if (due / 60 == std_now / 60) return true;
    /* Only tolerate a short delay AFTER the scheduled second, never before. */
    return allow_overdue && std_now >= due && std_now - due <= 5;
}

void dst_schedule_active_check(void) {
    if (active_check_alarm_id >= 0) cancel_alarm(active_check_alarm_id);
    active_check_alarm_id = -1;
    uint64_t delay_ms;
    if (!dst_get_next_wakeup_ms(&delay_ms)) return;
    if (delay_ms > (uint64_t)DST_ACTIVE_CHECK_MAX_SECONDS * 1000)
        delay_ms = (uint64_t)DST_ACTIVE_CHECK_MAX_SECONDS * 1000;
    if (delay_ms < 100) delay_ms = 100;
    active_check_alarm_id = add_alarm_in_us((int64_t)delay_ms * 1000,
                                            active_check_callback, NULL, true);
}

bool dst_active_check_due(void) {
    if (!active_check_pending) return false;
    active_check_pending = false;
    return true;
}

void dst_active_check_reset(void) {
    active_check_pending = true;
}

static void dst_config_changed(const char *key, uint8_t old_value, uint8_t new_value) {
    (void)key;
    if (old_value != new_value) dst_active_check_reset();
}

void dst_register_config_callbacks(void) {
    register_item_changed_callback(CONF_DST_OFFSET, dst_config_changed);
    register_item_changed_callback(CONF_DST_BEGIN_MON, dst_config_changed);
    register_item_changed_callback(CONF_DST_BEGIN_DAY, dst_config_changed);
    register_item_changed_callback(CONF_DST_BEGIN_HOUR, dst_config_changed);
    register_item_changed_callback(CONF_DST_BEGIN_MIN, dst_config_changed);
    register_item_changed_callback(CONF_DST_END_MON, dst_config_changed);
    register_item_changed_callback(CONF_DST_END_DAY, dst_config_changed);
    register_item_changed_callback(CONF_DST_END_HOUR, dst_config_changed);
    register_item_changed_callback(CONF_DST_END_MIN, dst_config_changed);
    register_item_changed_callback(CONF_DST_APPLIED, dst_config_changed);
}
