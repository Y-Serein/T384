#ifndef T384_LWIP_ARCH_CC_H
#define T384_LWIP_ARCH_CC_H

#include <stdint.h>
#include <string.h>

#include "ch32h417.h"

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#define LWIP_CHKSUM_ALGORITHM 3
#define LWIP_RAND() ((uint32_t)SysTick0->CNT ^ DBGMCU_GetCHIPID())

#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

#endif
