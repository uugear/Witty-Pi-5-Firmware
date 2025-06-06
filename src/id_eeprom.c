#include <hardware/gpio.h>

#include "id_eeprom.h"


bool eeprom_wp = true;


/**
 * Initialize the ID EEPROM manager
 */                                                      
void id_eeprom_init(void) {
	gpio_init(GPIO_ID_EEPROM_WRITE_PROTECTION);
    gpio_set_dir(GPIO_ID_EEPROM_WRITE_PROTECTION, GPIO_OUT);
    gpio_put(GPIO_ID_EEPROM_WRITE_PROTECTION, eeprom_wp);
}


/**
 * Control the write protection of ID EEPROM
 * 
 * @param on Whether to turn on write protection
 */
void id_eeprom_write_protection(bool on) {
    eeprom_wp = on;
    gpio_put(GPIO_ID_EEPROM_WRITE_PROTECTION, on);
}


/**
 * Check whether the write protection of ID EEPROM is ON
 *
 * @return true or false
 */
bool is_eeprom_write_protection_on(void) {
    return eeprom_wp;
}
