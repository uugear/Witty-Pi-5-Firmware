#ifndef _ADC_H_
#define _ADC_H_

#define ADC_CHANNEL_0       26
#define ADC_CHANNEL_1       27
#define ADC_CHANNEL_2       28
#define ADC_CHANNEL_3       29


/**
 * Initialize ADC channels
 */
void adc_channels_init(void);


/**
 * Read voltage in mV on specific channel
 * 
 * @param channel The ADC channel to read
 * @param msb Pointer to store MSB
 * @param lsb Pointer to store LSB
 * @return true if succeed, otherwise false
 */
bool read_voltage_mv(uint8_t channel, uint8_t *msb, uint8_t *lsb);


/**
 * Read current in mA on specific channel
 * 
 * @param channel The ADC channel to read
 * @param msb Pointer to store MSB
 * @param lsb Pointer to store LSB
 * @return true if succeed, otherwise false
 */
bool read_current_ma(uint8_t channel, uint8_t *msb, uint8_t *lsb);


/**
 * Get V-USB in mV
 * 
 * @return the voltage in mV
 */
uint16_t get_vusb_mv(void);


/**
 * Get V-IN in mV
 * 
 * @return the voltage in mV
 */
uint16_t get_vin_mv(void);


/**
 * Get V-OUT in mV
 * 
 * @return the voltage in mV
 */
uint16_t get_vout_mv(void);


/**
 * Get I-OUT in mA
 * 
 * @return the current in mA
 */
uint16_t get_iout_ma(void);

#endif