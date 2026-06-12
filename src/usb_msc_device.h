#ifndef _USB_MSC_DEVICE_H_
#define _USB_MSC_DEVICE_H_

#include <stdbool.h>

#define USB_MSC_AUTO_EJECT_WAIT_MS  500u

/**
 * Check whether emulated USB mass storage device is mounted
 *
 * @return true if USB-drive is mounted, false otherwise
 */
bool is_usb_msc_device_mounted(void);

/**
 * Ensure USB MSC is ejected before file operations.
 * Safe to call even if already ejected.
 */
void usb_msc_ensure_ejected(void);

/**
 * Mark the MSC medium as ejected and give the USB host some time to observe the state change
 */
void usb_msc_mark_ejected_and_wait(uint32_t wait_ms);

#endif
