#ifndef _BOOTSEL_BUTTON_H
#define _BOOTSEL_BUTTON_H

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "gpio.h"


#define BOOTSEL_LONG_PRESS_THRESHOLD	20000


bool __no_inline_not_in_flash_func(bb_get_bootsel_button)(void) {
  const uint CS_PIN_INDEX = 1;

  // Must disable interrupts, as interrupt handlers may be in flash, and we
  // are about to temporarily disable flash access!
  uint32_t flags = save_and_disable_interrupts();

  // Set chip select to Hi-Z
  hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                  GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                  IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

  // Note we can't call into any sleep functions in flash right now
  for (volatile int i = 0; i < 1000; ++i);

  // The HI GPIO registers in SIO can observe and control the 6 QSPI pins.
  // Note the button pulls the pin *low* when pressed.

  #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
  bool button_state = (sio_hw->gpio_hi_in & CS_BIT);

  // Need to restore the state of chip select, else we are going to have a
  // bad time when we return to code in flash!
  hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                  GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                  IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

  restore_interrupts(flags);

  return button_state;
}


static void check_bootsel_button(gpio_event_callback_t up, gpio_event_callback_t down, gpio_event_callback_t long_pressed) {
    static bool last_status = false;
    static int count = 0;
    static uint64_t long_push = 0;

    bool button = bb_get_bootsel_button();
    if (last_status != button && button) {  // Release BOOTSEL button
		if (up) {
		    up();
		}
    } else if (last_status != button && !button) { // Push BOOTSEL button
        if (down) {
            down();
        }
    }
    last_status = button;

    if (!button) {
        long_push++;
    } else {
        long_push = 0;
    }
    if (long_push > BOOTSEL_LONG_PRESS_THRESHOLD) { // Long-push BOOTSEL button
        count = 0;
        long_push = 0;
        if (long_pressed) {
            long_pressed();
        }
    }
}


#endif
