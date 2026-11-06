#ifndef MOCK_PICO_TIME_H
#define MOCK_PICO_TIME_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int32_t alarm_id_t;
static inline bool cancel_alarm(alarm_id_t id) { (void)id; return true; }
static inline alarm_id_t add_alarm_in_us(int64_t delay,
                                         int64_t (*callback)(alarm_id_t, void *),
                                         void *data, bool fire_if_past) {
    (void)delay; (void)callback; (void)data; (void)fire_if_past;
    return 1;
}
#endif
