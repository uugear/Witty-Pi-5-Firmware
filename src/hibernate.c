#include <stdbool.h>
#include <stdint.h>

#include <pico/error.h>
#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <hardware/gpio.h>
#include <hardware/powman.h>
#include <tusb.h>

#include "hibernate.h"
#include "button.h"
#include "rtc.h"
#include "ts.h"
#include "main.h"
#include "conf.h"
#include "log.h"
#include "fatfs_disk.h"
#include "usb_msc_device.h"
#include "power.h"

// POWMAN pwrup slots used by WP5.
#define SRC_BUTTON      0
#define SRC_RTC_INT     1
#define SRC_TS_INT      2

#define HIBERNATE_SCRATCH_CONTEXT 0
#define HIBERNATE_CONTEXT_MAGIC         0xA55A0000u
#define HIBERNATE_CONTEXT_MAGIC_MASK    0xFFFF0000
#define HIBERNATE_CONTEXT_RTC_ALARM_TYPE_MASK   0x000000FFu
#define HIBERNATE_CONTEXT_FLAG_VIN_RECOVERABLE  0x00000100u

// Avoid a zero-delay POWMAN alarm loop if the configuration is invalid.
#define HIBERNATE_MIN_PULSE_INTERVAL_S  1u

static powman_power_state off_state;
static powman_power_state on_state;
static uint32_t last_wakeup_flags = WAKEUP_SOURCE_RESET;
static bool resumed_from_hibernate = false;
static uint32_t hibernate_block_flags = HIBERNATE_BLOCK_NONE;
static absolute_time_t hibernate_usb_grace_until;
static bool forced_entry_requested = false;

bool hibernating = false;
int wokeup_by = WOKEUP_BY_NONE;

static uint32_t hibernate_make_context(uint8_t rtc_alarm_type, bool vin_recoverable) {
    uint32_t context = HIBERNATE_CONTEXT_MAGIC |
                       ((uint32_t)rtc_alarm_type & HIBERNATE_CONTEXT_RTC_ALARM_TYPE_MASK);
    if (vin_recoverable) {
        context |= HIBERNATE_CONTEXT_FLAG_VIN_RECOVERABLE;
    }
    return context;
}

static bool hibernate_context_is_valid(uint32_t context) {
    return (context & HIBERNATE_CONTEXT_MAGIC_MASK) == HIBERNATE_CONTEXT_MAGIC;
}

static uint8_t hibernate_context_get_rtc_alarm_type(uint32_t context) {
    if (!hibernate_context_is_valid(context)) {
        return ALARM_TYPE_NONE;
    }
    return (uint8_t)(context & HIBERNATE_CONTEXT_RTC_ALARM_TYPE_MASK);
}

static bool hibernate_context_get_vin_recoverable(uint32_t context) {
    return hibernate_context_is_valid(context) &&
           ((context & HIBERNATE_CONTEXT_FLAG_VIN_RECOVERABLE) != 0);
}

static uint32_t hibernate_decode_last_swcore_pwrup(void) {
    return powman_hw->last_swcore_pwrup & 0x7Fu;
}

static int hibernate_decode_wokeup_by(uint32_t wake_flags) {
    if (wake_flags & WAKEUP_SOURCE_BUTTON) {
        return WOKEUP_BY_BUTTON;
    }
    if (wake_flags & WAKEUP_SOURCE_RTC) {
        return WOKEUP_BY_RTC_ALARM;
    }
    if (wake_flags & WAKEUP_SOURCE_TS) {
        return WOKEUP_BY_TS_ALARM;
    }
    if (wake_flags & WAKEUP_SOURCE_TIMER) {
        return WOKEUP_BY_PULSE;
    }
    return WOKEUP_BY_NONE;
}

static void hibernate_init_wake_gpio(uint gpio) {
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_IN);
    gpio_pull_up(gpio);
}

static bool hibernate_ts_wakeup_enabled(void) {
    return conf_get(CONF_BELOW_TEMP_ACTION) == TEMP_ACTION_STARTUP ||
           conf_get(CONF_OVER_TEMP_ACTION) == TEMP_ACTION_STARTUP;
}

static void hibernate_prepare_usb_for_sleep(void) {
    // Tell the USB host that the device is going away before POWMAN sleep.
    tud_disconnect();

    // Let TinyUSB process the disconnect and give the host time to observe it.
    absolute_time_t until = make_timeout_time_ms(250);
    while (!time_reached(until)) {
        tud_task();
        sleep_ms(1);
    }

    stdio_flush();
    stdio_deinit_all();
}

uint32_t hibernate_get_active_gpio_flags(void) {
    uint32_t flags = 0;

    if (!gpio_get(GPIO_BUTTON)) {
        flags |= HIBERNATE_ACTIVE_BUTTON;
    }
    if (!gpio_get(GPIO_RTC_INT)) {
        flags |= HIBERNATE_ACTIVE_RTC;
    }
    if (hibernate_ts_wakeup_enabled() && !gpio_get(GPIO_TS_INT)) {
        flags |= HIBERNATE_ACTIVE_TS;
    }

    return flags;
}

void hibernate_init(void) {
    // Allow POWMAN power-down while a debug probe is attached.
    powman_set_debug_power_request_ignored(true);

    // If it is resumed from hibernation
    uint32_t context = powman_hw->scratch[HIBERNATE_SCRATCH_CONTEXT];
    resumed_from_hibernate = hibernate_context_is_valid(context);

    // Cache the last hardware wake source before configuring a new sleep.
    if (resumed_from_hibernate) {
        last_wakeup_flags = hibernate_decode_last_swcore_pwrup();
        uint8_t rtc_alarm_type = hibernate_context_get_rtc_alarm_type(context);
        rtc_restore_alarm_type_after_hibernate(rtc_alarm_type);
        bool vin_recoverable = hibernate_context_get_vin_recoverable(context);
        power_restore_vin_recoverable_after_hibernate(vin_recoverable);
    } else {
        last_wakeup_flags = WAKEUP_SOURCE_RESET;
        rtc_restore_alarm_type_after_hibernate(ALARM_TYPE_NONE);
        power_restore_vin_recoverable_after_hibernate(false);
    }

    powman_hw->scratch[HIBERNATE_SCRATCH_CONTEXT] = 0;
    
    wokeup_by = hibernate_decode_wokeup_by(last_wakeup_flags);

    hibernate_usb_grace_until = make_timeout_time_ms(HIBERNATE_USB_ENUM_GRACE_MS);

    // Prepare the power states used by the old, proven POWMAN path.
    off_state = POWMAN_POWER_STATE_NONE;

    on_state = POWMAN_POWER_STATE_NONE;
    on_state = powman_power_state_with_domain_on(on_state, POWMAN_POWER_DOMAIN_SWITCHED_CORE);
    on_state = powman_power_state_with_domain_on(on_state, POWMAN_POWER_DOMAIN_XIP_CACHE);

    // Keep the wake GPIO configuration predictable.
    hibernate_init_wake_gpio(GPIO_BUTTON);
    hibernate_init_wake_gpio(GPIO_RTC_INT);
    hibernate_init_wake_gpio(GPIO_TS_INT);
}

uint32_t hibernate_get_wakeup_flags(void) {
    return last_wakeup_flags;
}

bool hibernate_can_enter(void) {

    hibernate_block_flags = HIBERNATE_BLOCK_NONE;

    // Hibernation can only happen at OFF state
    if (current_rpi_state != STATE_OFF) {
        hibernate_block_flags |= HIBERNATE_BLOCK_RPI_NOT_OFF;
    }

    // Do not hibernate while the host may still be accessing the virtual disk.
    if (is_usb_msc_device_mounted()) {
        hibernate_block_flags |= HIBERNATE_BLOCK_USB_MOUNTED;
    }

    // Be conservative when the local FatFs volume is not available.
    if (!is_fatfs_mounted()) {
        hibernate_block_flags |= HIBERNATE_BLOCK_FATFS_DOWN;
    }

    // Already-active wake lines should be handled before entering POWMAN.
    if (hibernate_get_active_gpio_flags() != 0u) {
        hibernate_block_flags |= HIBERNATE_BLOCK_GPIO_ACTIVE;
    }

    // Give some time for host to enumerate the USB device
    if (!time_reached(hibernate_usb_grace_until)) {
        hibernate_block_flags |= HIBERNATE_BLOCK_USB_GRACE;
    }

    return hibernate_block_flags == HIBERNATE_BLOCK_NONE;
}

static void hibernate_flush_persistent_tasks(void) {
    process_log_task();
    process_conf_task();

    if (!is_usb_msc_device_mounted() && is_fatfs_mounted()) {
        // Force RAM configuration to be synchronized before hibernation.
        conf_sync();

        if (is_log_saving_to_file()) {
            save_logs_to_file();
        }
    }

    process_log_task();
    stdio_flush();
}

static void hibernate_configure_gpio_wakeup(void) {
    hibernate_init_wake_gpio(GPIO_BUTTON);
    hibernate_init_wake_gpio(GPIO_RTC_INT);
    hibernate_init_wake_gpio(GPIO_TS_INT);

    // Use low-level wake. The lines are active-low on WP5.
    powman_enable_gpio_wakeup(SRC_BUTTON, GPIO_BUTTON, false, false);
    powman_enable_gpio_wakeup(SRC_RTC_INT, GPIO_RTC_INT, false, false);
    if (hibernate_ts_wakeup_enabled()) {
        powman_enable_gpio_wakeup(SRC_TS_INT, GPIO_TS_INT, false, false);
    }
}

static void hibernate_configure_timer_wakeup(void) {
    uint32_t interval_s = conf_get(CONF_PULSE_INTERVAL);
    if (interval_s < HIBERNATE_MIN_PULSE_INTERVAL_S) {
        interval_s = HIBERNATE_MIN_PULSE_INTERVAL_S;
    }

    uint64_t now_ms = powman_timer_get_ms();
    powman_enable_alarm_wakeup_at_ms(now_ms + (uint64_t)interval_s * 1000u);
}

int hibernate_enter(void) {
    if (hibernate_is_forced_entry_requested() && is_usb_msc_device_mounted()) {
        usb_msc_mark_ejected_and_wait(USB_MSC_AUTO_EJECT_WAIT_MS);
    }

    if (!hibernate_can_enter()) {
        return PICO_ERROR_INVALID_STATE;
    }

    hibernate_flush_persistent_tasks();

    // Re-check after flushing because callbacks or file tasks may have run.
    if (!hibernate_can_enter()) {
        return PICO_ERROR_INVALID_STATE;
    }

    hibernate_configure_gpio_wakeup();
    hibernate_configure_timer_wakeup();
    
    uint8_t rtc_alarm_type = rtc_get_alarm_type();
    bool vin_recoverable = power_is_vin_recoverable();
    powman_hw->scratch[HIBERNATE_SCRATCH_CONTEXT] = hibernate_make_context(rtc_alarm_type, vin_recoverable);

    bool valid_state = powman_configure_wakeup_state(off_state, on_state);
    if (!valid_state) {
        return PICO_ERROR_INVALID_STATE;
    }

    // Reboot to the normal vector after switched-core power-up.
    powman_hw->boot[0] = 0;
    powman_hw->boot[1] = 0;
    powman_hw->boot[2] = 0;
    powman_hw->boot[3] = 0;

    hibernating = true;

    hibernate_prepare_usb_for_sleep();

    int rc = powman_set_power_state(off_state);
    if (rc != PICO_OK) {
        hibernating = false;
        powman_hw->scratch[HIBERNATE_SCRATCH_CONTEXT] = 0;
        return rc;
    }

    while (true) {
        __wfi();
    }
}

bool hibernate_was_resumed(void) {
    return resumed_from_hibernate;
}

uint32_t hibernate_get_block_flags(void) {
    return hibernate_block_flags;
}

void hibernate_skip_usb_grace(void) {
    hibernate_usb_grace_until = get_absolute_time();
}

void hibernate_request_forced_entry(void) {
    forced_entry_requested = true;
}

void hibernate_clear_forced_entry(void) {
    forced_entry_requested = false;
}

bool hibernate_is_forced_entry_requested(void) {
    return forced_entry_requested;
}