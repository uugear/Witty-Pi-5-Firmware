#include <hardware/adc.h>

#include "adc.h"


/**
 * Initialize ADC channels
 */
void adc_channels_init(void) {
    adc_init();
    
    adc_gpio_init(ADC_CHANNEL_0);
    adc_gpio_init(ADC_CHANNEL_1);
    adc_gpio_init(ADC_CHANNEL_2);
    adc_gpio_init(ADC_CHANNEL_3);
}


/**
 * Read voltage in mV on specific channel
 * 
 * @param channel The ADC channel to read
 * @param msb Pointer to store MSB
 * @param lsb Pointer to store LSB
 * @return true if succeed, otherwise false
 */
bool read_voltage_mv(uint8_t channel, uint8_t *msb, uint8_t *lsb) {

    if (channel < 0 || channel > 3) {
        return false;
    }

    adc_select_input(channel);

    uint16_t value = adc_read();
    
    uint32_t result = ((uint32_t)value * 580800) >> 16;    // equals to value*36300/4096
    
    if (msb) {
        *msb = result >> 8;
    }
    
    if (lsb) {
        *lsb = result & 0xFF;
    }
    
    return true;
}


/**
 * Read current in mA on specific channel
 * 
 * @param channel The ADC channel to read
 * @param msb Pointer to store MSB
 * @param lsb Pointer to store LSB
 * @return true if succeed, otherwise false
 */
bool read_current_ma(uint8_t channel, uint8_t *msb, uint8_t *lsb) {
    
    if (channel < 0 || channel > 3) {
        return false;
    }
    
    adc_select_input(channel);

    uint16_t value = adc_read();
    
    uint32_t result = ((uint32_t)value * 844800) >> 19;     // equals to value*6600/4096
    
    if (msb) {
        *msb = result >> 8;
    }
    
    if (lsb) {
        *lsb = result & 0xFF;
    }
    
    return true;
}


// Get voltage in mV
uint16_t get_voltage_mv(uint8_t channel) {
    uint8_t msb;
    uint8_t lsb;
    if (read_voltage_mv(channel, &msb, &lsb)) {
        return (((uint16_t)msb << 8) | lsb);
    }
    return 0;
}


/**
 * Get V-USB in mV
 * 
 * @return the voltage in mV
 */
uint16_t get_vusb_mv(void) {
    return get_voltage_mv(0);
}


/**
 * Get V-IN in mV
 * 
 * @return the voltage in mV
 */
uint16_t get_vin_mv(void) {
    return get_voltage_mv(1);
}


/**
 * Get V-OUT in mV
 * 
 * @return the voltage in mV
 */
uint16_t get_vout_mv(void) {
    return get_voltage_mv(2);
}


/**
 * Get I-OUT in mA
 * 
 * @return the current in mA
 */
uint16_t get_iout_ma(void) {
    uint8_t msb;
    uint8_t lsb;
    if (read_current_ma(3, &msb, &lsb)) {
        return (((uint16_t)msb << 8) | lsb);
    }
    return 0;
}
