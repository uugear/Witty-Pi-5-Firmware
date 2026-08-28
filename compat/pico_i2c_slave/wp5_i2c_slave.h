#pragma once

#include <hardware/i2c.h>
#include <pico/i2c_slave.h>

void wp5_i2c_slave_init(
    i2c_inst_t *i2c,
    uint8_t address,
    i2c_slave_handler_t handler
);

void wp5_i2c_slave_deinit(i2c_inst_t *i2c);
