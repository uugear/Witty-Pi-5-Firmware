#ifndef _PRODUCT_H_
#define _PRODUCT_H_

#define PRODUCT_WITTY_PI_5_HAT_PLUS  1
#define PRODUCT_WITTY_PI_5_MINI      2

#ifndef WP5_PRODUCT
#error "WP5_PRODUCT must be defined by the build system"
#endif

#if WP5_PRODUCT == PRODUCT_WITTY_PI_5_HAT_PLUS

#define PRODUCT_NAME    "Witty Pi 5 HAT+"
#define FIRMWARE_ID     0x51

#elif WP5_PRODUCT == PRODUCT_WITTY_PI_5_MINI

#define PRODUCT_NAME    "Witty Pi 5 Mini"
#define FIRMWARE_ID     0x52

#else
#error "Unknown WP5_PRODUCT"
#endif

#endif