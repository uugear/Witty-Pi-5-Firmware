#ifndef _HIBERNATE_H_
#define _HIBERNATE_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Low-level RP2350 POWMAN wake sources.
 *
 * These values are bit masks derived from powman_hw->last_swcore_pwrup:
 *   0 -> reset/chip reset
 *   1 -> pwrup0
 *   2 -> pwrup1
 *   3 -> pwrup2
 *   4 -> pwrup3
 *   5 -> coresight_pwrup
 *   6 -> alarm_pwrup
 *
 * WP5 maps pwrup0/1/2 to button/RTC_INT/TS_INT respectively. The timer
 * source is the POWMAN alarm wake source and is used for the low-power pulse
 * and VIN recovery check path.
 */
#define WAKEUP_SOURCE_RESET      (1u << 0)
#define WAKEUP_SOURCE_BUTTON     (1u << 1)
#define WAKEUP_SOURCE_RTC        (1u << 2)
#define WAKEUP_SOURCE_TS         (1u << 3)
#define WAKEUP_SOURCE_PWRUP3     (1u << 4)
#define WAKEUP_SOURCE_CORESIGHT  (1u << 5)
#define WAKEUP_SOURCE_TIMER      (1u << 6)

/*
 * High-level reason why Raspberry Pi is powered on or why RP2350 did a
 * short low-power pulse. These values are user-facing/log-facing reasons,
 * not raw RP2350 POWMAN wake sources.
 */
#define WOKEUP_BY_NONE          0
#define WOKEUP_BY_PULSE         1
#define WOKEUP_BY_RTC_ALARM     2
#define WOKEUP_BY_TS_ALARM      3
#define WOKEUP_BY_VOLTAGE       4
#define WOKEUP_BY_BUTTON        5

/*
 * Optional diagnostic flags for active-low wake GPIO levels. These are not
 * replacements for WAKEUP_SOURCE_*; they are only useful for logging and for
 * deciding whether it is safe to enter hibernation.
 */
#define HIBERNATE_ACTIVE_BUTTON  (1u << 0)
#define HIBERNATE_ACTIVE_RTC     (1u << 1)
#define HIBERNATE_ACTIVE_TS      (1u << 2)

/**
 * Possible reason that blocks hibernation
 */
#define HIBERNATE_BLOCK_NONE          0
#define HIBERNATE_BLOCK_RPI_NOT_OFF   (1u << 0)
#define HIBERNATE_BLOCK_USB_MOUNTED   (1u << 1)
#define HIBERNATE_BLOCK_FATFS_DOWN    (1u << 2)
#define HIBERNATE_BLOCK_GPIO_ACTIVE   (1u << 3)
#define HIBERNATE_BLOCK_USB_GRACE     (1u << 4)

#define HIBERNATE_USB_ENUM_GRACE_MS     3000u

extern bool hibernating;
extern int wokeup_by;

/**
 * Initialize the hibernate manager and cache the latest POWMAN wake source.
 *
 * This should be called once during firmware startup after basic hardware
 * initialization is available. It does not enter hibernation.
 */
void hibernate_init(void);

/**
 * Check whether it is currently safe to enter hibernation.
 *
 * This function is intentionally conservative. It returns false when the Pi
 * is not in OFF state, when the USB MSC device is mounted, when FatFs is not
 * mounted, or when any active-low wake GPIO is already asserted.
 */
bool hibernate_can_enter(void);

/**
 * Enter POWMAN hibernation.
 *
 * The function normally does not return. On error it returns a Pico SDK error
 * code, such as PICO_ERROR_INVALID_STATE.
 */
int hibernate_enter(void);

/**
 * Get the latest low-level RP2350 POWMAN wake source flags.
 *
 * The returned value is a bit mask made from WAKEUP_SOURCE_* definitions.
 */
uint32_t hibernate_get_wakeup_flags(void);

/**
 * Get active-low wake GPIO levels for diagnostics.
 *
 * The returned value is a bit mask made from HIBERNATE_ACTIVE_* definitions.
 * This is not the authoritative wake source. The authoritative source is
 * hibernate_get_wakeup_flags().
 */
uint32_t hibernate_get_active_gpio_flags(void);

/**
 * If hibernation happened at least once
 */
bool hibernate_was_resumed(void);

/**
 * Get the flags for hibernation blocking
 */
uint32_t hibernate_get_block_flags(void);

/**
 * Skip usb grace for next hibernate request
 */
void hibernate_skip_usb_grace(void);

/**
 * Request forced entry for hibernation
 */
void hibernate_request_forced_entry(void);

/**
 * Clear forced entry for hibernation
 */
void hibernate_clear_forced_entry(void);

/**
 * Check if forced entry is requested
 */
bool hibernate_is_forced_entry_requested(void);

#endif
