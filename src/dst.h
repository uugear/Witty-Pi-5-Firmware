#ifndef WP5_DST_H
#define WP5_DST_H

#include <stdbool.h>
#include <stdint.h>
#include "dst_time.h"

bool dst_get_rule(DstRule *rule);
bool dst_get_standard_now(int64_t *standard_ts);
int64_t dst_action_to_standard(int64_t local_action_ts);
int64_t dst_action_to_hardware_local(int64_t local_action_ts);
bool dst_get_next_wakeup_ms(uint64_t *delay_ms);
/* Return true only when a clock change was actually committed. */
bool dst_process_due_transition(void);
/* Check pending RX8025 alarms and skipped-time actions against continuous time. */
bool dst_configured_alarm_ready(bool allow_overdue);
void dst_schedule_active_check(void);
bool dst_active_check_due(void);
void dst_active_check_reset(void);
void dst_register_config_callbacks(void);

#endif
