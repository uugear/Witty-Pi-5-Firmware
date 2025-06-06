#ifndef _UTIL_H_
#define _UTIL_H_

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

#define BIT_VALUE(bit) (1 << (bit))

/**
 * Get actual value for a binary-coded decimal (BCD) byte
 * 
 * @param bcd The BCD byte
 * @return The actual value
 */
static inline uint8_t bcd_to_dec(uint8_t bcd) {
	return ((bcd >> 4) * 10) + (bcd & 0x0F);
}


/**
 * Get binary-coded decimal (BCD) byte for a value
 * 
 * @param dec The value
 * @return The binary-coded decimal (BCD) byte
 */
static inline uint8_t dec_to_bcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}


#endif