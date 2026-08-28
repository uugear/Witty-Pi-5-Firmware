/*
 * Copyright (c) 2021 Valentin Milea <valentin.milea@gmail.com>
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "wp5_i2c_slave.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

typedef struct i2c_slave {
    i2c_slave_handler_t handler;
    bool transfer_in_progress;
} i2c_slave_t;

static i2c_slave_t i2c_slaves[2];


static void __isr __not_in_flash_func(i2c_slave_irq_handler)(void) {
    uint i2c_index = __get_current_exception() - VTABLE_FIRST_IRQ - I2C0_IRQ;
    i2c_slave_t *slave = &i2c_slaves[i2c_index];
    i2c_inst_t *i2c = i2c_get_instance(i2c_index);
    i2c_hw_t *hw = i2c_get_hw(i2c);

    uint32_t intr_stat = hw->intr_stat;
    if (intr_stat == 0) {
        return;
    }
    bool do_finish_transfer = false;
    if (intr_stat & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) {
        hw->clr_tx_abrt;
        do_finish_transfer = true;
    }
    if (intr_stat & I2C_IC_INTR_STAT_R_START_DET_BITS) {
        hw->clr_start_det;
        do_finish_transfer = true;
    }
    if (intr_stat & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        hw->clr_stop_det;
        do_finish_transfer = true;
    }
    if (do_finish_transfer && slave->transfer_in_progress) {
        slave->handler(i2c, I2C_SLAVE_FINISH);
        slave->transfer_in_progress = false;
    }
    if (intr_stat & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        slave->transfer_in_progress = true;
        slave->handler(i2c, I2C_SLAVE_RECEIVE);
    }
    if (intr_stat & I2C_IC_INTR_STAT_R_RD_REQ_BITS) {
        hw->clr_rd_req;
        slave->transfer_in_progress = true;
        slave->handler(i2c, I2C_SLAVE_REQUEST);
    }
}

void wp5_i2c_slave_init(i2c_inst_t *i2c, uint8_t address, i2c_slave_handler_t handler) {
    assert(i2c == i2c0 || i2c == i2c1);
    assert(handler != NULL);

    uint32_t irq_state = save_and_disable_interrupts();

    uint i2c_index = i2c_hw_index(i2c);
    uint irq_num = I2C0_IRQ + i2c_index;

    i2c_slave_t *slave = &i2c_slaves[i2c_index];
    i2c_hw_t *hw = i2c_get_hw(i2c);

    /*
     * Keep the CPU-side interrupt path disabled until the complete
     * slave infrastructure is ready.
     */
    irq_set_enabled(irq_num, false);

    /*
     * Keep peripheral interrupts masked while the slave software
     * and IRQ infrastructure are being prepared.
     *
     * At this point the controller is expected to be non-slave:
     * - after i2c_init() during boot, or
     * - after wp5_i2c_slave_deinit() during runtime resume.
     */
    hw->intr_mask = 0;

    /*
     * Reset software transaction state before exposing the slave
     * to the external I2C master.
     */
    slave->handler = handler;
    slave->transfer_in_progress = false;

    /*
     * Install the ISR before the hardware is allowed to acknowledge
     * the slave address.
     */
    irq_set_exclusive_handler(irq_num, i2c_slave_irq_handler);

    /*
     * Prepare all required slave interrupt sources.
     */
    hw->intr_mask =
            I2C_IC_INTR_MASK_M_RX_FULL_BITS |
            I2C_IC_INTR_MASK_M_RD_REQ_BITS |
            I2C_IC_INTR_MASK_M_TX_ABRT_BITS |
            I2C_IC_INTR_MASK_M_STOP_DET_BITS |
            I2C_IC_INTR_MASK_M_START_DET_BITS;

    /*
     * Clear any NVIC pending state that may have appeared while
     * configuring the peripheral interrupt mask.
     */
    irq_clear(irq_num);

    /*
     * Enable the CPU-side IRQ first. Global interrupts are still
     * disabled, so the ISR cannot run yet.
     */
    irq_set_enabled(irq_num, true);

    /*
     * Enable slave hardware last.
     *
     * From this point onward the controller may acknowledge the
     * external master's address, but the handler, interrupt mask,
     * and NVIC route are already fully prepared.
     */
    i2c_set_slave_mode(i2c, true, address);

    restore_interrupts(irq_state);
}

void wp5_i2c_slave_deinit(i2c_inst_t *i2c) {
    assert(i2c == i2c0 || i2c == i2c1);

    uint32_t irq_state = save_and_disable_interrupts();

    uint i2c_index = i2c_hw_index(i2c);
    uint irq_num = I2C0_IRQ + i2c_index;

    i2c_slave_t *slave = &i2c_slaves[i2c_index];
    i2c_hw_t *hw = i2c_get_hw(i2c);

    assert(slave->handler);

    /*
     * Stop CPU-side slave interrupt handling first.
     */
    irq_set_enabled(irq_num, false);

    /*
     * Prevent any new peripheral slave interrupt from being generated.
     */
    hw->intr_mask = 0;

    /*
     * Stop acknowledging the external slave address before dismantling
     * the software-side handler state.
     */
    i2c_set_slave_mode(i2c, false, 0);

    /*
     * Now it is safe to tear down the software transaction state.
     */
    slave->transfer_in_progress = false;

    irq_remove_handler(irq_num, i2c_slave_irq_handler);

    slave->handler = NULL;

    restore_interrupts(irq_state);
}
