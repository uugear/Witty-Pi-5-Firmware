#ifndef _ID_EEPROM_H_
#define _ID_EEPROM_H_

#include <stdbool.h>


#define GPIO_ID_EEPROM_WRITE_PROTECTION    	10


/**
 * Initialize the ID EEPROM manager
 */                                                      
void id_eeprom_init(void);


/**
 * Control the write protection of ID EEPROM
 * 
 * @param on Whether to turn on write protection
 */
void id_eeprom_write_protection(bool on);


/**
 * Check whether the write protection of ID EEPROM is ON
 *
 * @return true or false
 */
bool is_eeprom_write_protection_on(void);


#endif