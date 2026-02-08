#ifndef _USB_MSC_DEVICE_H_
#define _USB_MSC_DEVICE_H_

#include <stdbool.h>

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

#endif
