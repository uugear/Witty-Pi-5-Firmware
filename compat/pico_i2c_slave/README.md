# Witty Pi 5 Pico I2C Slave Compatibility Layer

This module is based on pico_i2c_slave from Raspberry Pi Pico SDK 2.2.0,
commit a1438dff1d38bd9c65dbd693f0e5db4b9ae91779.

Witty Pi 5 temporarily takes its external I2C slave offline while
persistent flash writes are in progress.

The stock Pico SDK slave lifecycle tears down and enables parts of the
IRQ and hardware state in an order that is unsafe for Witty Pi 5's
runtime suspend/resume use case.

This local implementation provides a safer initialization and
deinitialization order. The Witty Pi firmware additionally resets the
entire I2C1 peripheral between suspend and resume to guarantee a clean
controller state.

When upgrading Pico SDK, compare this implementation with the upstream
pico_i2c_slave module and reevaluate whether this compatibility layer
is still required.
