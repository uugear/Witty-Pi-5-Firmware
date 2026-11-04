#include <hardware/adc.h>
#include <hardware/structs/adc.h>
#include <stdint.h>

#include "adc.h"


// Keep the five-conversion START2 instruction sequence used in the R3 Flash-XIP test.
// The assembly routine reads five ADC results into samples and their CS.ERR flags
// into errors. The current estimator uses the raw results, matching R3 behavior.
extern void wp5_adc_convert5_start2(uint8_t *errors,
                                    volatile uint32_t *cs_set_alias,
                                    volatile uint32_t *cs,
                                    uint16_t *samples);

#define ADC_FILTER_SAMPLE_COUNT 15u
#define ADC_FILTER_TRIM_PER_SIDE 3u
#define ADC_FILTER_RETAINED_COUNT 9u

/**
 * Read 15 ADC samples and return the rounded 9-of-15 trimmed mean.
 *
 * Three consecutive five-sample calls preserve the sampling structure of
 * the R3 Flash-XIP comparison. There is no deliberate delay between calls.
 * This function should not be reentered while a conversion is in progress.
 */
static uint16_t adc_read_trimmed_mean9(uint8_t channel) {
    uint16_t samples[ADC_FILTER_SAMPLE_COUNT];
    uint8_t errors[ADC_FILTER_SAMPLE_COUNT];

    adc_select_input(channel);

    // On RP2350, the atomic SET alias is 0x2000 bytes above the CS register.
    volatile uint32_t *const cs = &adc_hw->cs;
    volatile uint32_t *const cs_set_alias =
        (volatile uint32_t *)((uintptr_t)cs + 0x2000u);

    wp5_adc_convert5_start2(errors, cs_set_alias, cs, samples);
    wp5_adc_convert5_start2(errors + 5, cs_set_alias, cs, samples + 5);
    wp5_adc_convert5_start2(errors + 10, cs_set_alias, cs, samples + 10);

    // Sort the 15 samples in place. No dynamic allocation is required.
    for (uint32_t i = 1; i < ADC_FILTER_SAMPLE_COUNT; ++i) {
        uint16_t value = samples[i];
        uint32_t j = i;
        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            --j;
        }
        samples[j] = value;
    }

    uint32_t sum = 0;
    for (uint32_t i = ADC_FILTER_TRIM_PER_SIDE;
         i < ADC_FILTER_SAMPLE_COUNT - ADC_FILTER_TRIM_PER_SIDE;
         ++i) {
        sum += samples[i];
    }
    // Round to the nearest ADC count, as in the R3 offline analysis.
    return (uint16_t)((sum + ADC_FILTER_RETAINED_COUNT / 2u) /
                      ADC_FILTER_RETAINED_COUNT);
}

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

    if (channel > 3u) {
        return false;
    }

    uint16_t value = adc_read_trimmed_mean9(channel);
    
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
    
    if (channel > 3u) {
        return false;
    }
    
    uint16_t value = adc_read_trimmed_mean9(channel);
    
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
