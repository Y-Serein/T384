#ifndef T384_LWIP_ARCH_CC_H
#define T384_LWIP_ARCH_CC_H

#include <stdint.h>
#include <string.h>

#include "ch32h417.h"
#include "t384_raw16.h"

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#define LWIP_CHKSUM_ALGORITHM 3
#define LWIP_RAND() ((uint32_t)SysTick0->CNT ^ DBGMCU_GetCHIPID())

#if T384_NETWORK_ON_V5F && defined(Core_V5F)
/* Relocate lwIP's generated heap/memp arrays out of the 19 KiB V5F local
 * .bss window.  The linker reserves .t384_net_heap in shared SRAM. */
#define LWIP_DECLARE_MEMORY_ALIGNED(variable_name, size) \
    uint8_t variable_name[LWIP_MEM_ALIGN_BUFFER(size)] \
        __attribute__((section(".t384_net_heap"), aligned(32)))
#endif

/*
 * lwIP's stock checksum-on-copy helper performs a full memcpy and then reads
 * the destination again. Fuse both passes for the RAW16 TCP COPY path while
 * preserving LWIP_CHKSUM_ALGORITHM=3 byte order and odd-boundary behavior.
 */
static inline uint16_t t384_lwip_chksum_copy(void *destination,
                                             const void *source,
                                             uint16_t length)
{
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    const uint32_t source_odd = (uint32_t)(uintptr_t)src & 1u;
    uint32_t sum = 0u;
    uint16_t tail = 0u;

    if (source_odd != 0u && length > 0u) {
        const uint8_t value = *src++;
        *dst++ = value;
        ((uint8_t *)&tail)[1] = value;
        --length;
    }

    if (((uint32_t)(uintptr_t)src & 3u) != 0u && length > 1u) {
        const uint8_t first = src[0];
        const uint8_t second = src[1];
        dst[0] = first;
        dst[1] = second;
        sum += (uint32_t)first | ((uint32_t)second << 8);
        src += 2;
        dst += 2;
        length = (uint16_t)(length - 2u);
    }

    while (length > 7u) {
        uint32_t first_word;
        uint32_t second_word;
        /* The byte/halfword prefix above aligns every eight-byte iteration.
         * Keep alias-safe memcpy, but avoid byte loads and stack assembly on
         * the V3F when reading a frame from the V5F memory banks. */
        const uint8_t *aligned_src =
            (const uint8_t *)__builtin_assume_aligned(src, 4u);
        memcpy(&first_word, aligned_src, sizeof(first_word));
        memcpy(dst, &first_word, sizeof(first_word));
        src += 4;
        dst += 4;
        memcpy(&second_word, aligned_src + 4u, sizeof(second_word));
        memcpy(dst, &second_word, sizeof(second_word));
        src += 4;
        dst += 4;

        uint32_t next = sum + first_word;
        if (next < sum) {
            ++next;
        }
        sum = next + second_word;
        if (sum < next) {
            ++sum;
        }
        length = (uint16_t)(length - 8u);
    }

    sum = (sum >> 16) + (sum & 0xffffu);

    while (length > 1u) {
        const uint8_t first = src[0];
        const uint8_t second = src[1];
        dst[0] = first;
        dst[1] = second;
        sum += (uint32_t)first | ((uint32_t)second << 8);
        src += 2;
        dst += 2;
        length = (uint16_t)(length - 2u);
    }

    if (length > 0u) {
        const uint8_t value = *src;
        *dst = value;
        ((uint8_t *)&tail)[0] = value;
    }

    sum += tail;
    sum = (sum >> 16) + (sum & 0xffffu);
    sum = (sum >> 16) + (sum & 0xffffu);
    if (source_odd != 0u) {
        sum = ((sum & 0xffu) << 8) | ((sum >> 8) & 0xffu);
    }
    return (uint16_t)sum;
}

#if defined(LWIP_CHECKSUM_ON_COPY) && LWIP_CHECKSUM_ON_COPY
#define LWIP_CHKSUM_COPY(dst, src, len) \
    t384_lwip_chksum_copy((dst), (src), (len))
#endif

#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

#endif
