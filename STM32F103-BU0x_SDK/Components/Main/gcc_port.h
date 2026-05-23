#ifndef GCC_PORT_H
#define GCC_PORT_H

/* Keil ARMCC compatibility for arm-none-eabi-gcc */
#ifndef __packed
#define __packed __attribute__((packed))
#endif

#include "hal_usart.h"
#include "hal_timer.h"

#endif
