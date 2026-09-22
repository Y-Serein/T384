#ifndef T384_V5F_NET_MEMORY_H
#define T384_V5F_NET_MEMORY_H

#include <stdint.h>

#include "t384_raw16.h"

/*
 * V5F data-plane storage is deliberately explicit.  These attributes are
 * empty for the V3F-network fallback, so the 256/384 path keeps its original
 * linker placement.
 */
#if T384_NETWORK_ON_V5F && defined(Core_V5F)
#define T384_NET_HTTP_STORAGE \
    __attribute__((section(".t384_net_http"), aligned(32)))
#define T384_NET_HEAP_STORAGE \
    __attribute__((section(".t384_net_heap"), aligned(32)))
#define T384_NET_SCRATCH_STORAGE T384_NET_HEAP_STORAGE
#define T384_NET_CONTROL_STORAGE T384_NET_HEAP_STORAGE
#define T384_NET_NCM_STORAGE \
    __attribute__((section(".t384_net_ncm"), aligned(32)))
#define T384_V5F_LWIP_HEAP_BYTES (32u * 1024u)
extern uint8_t t384_v5f_lwip_heap[T384_V5F_LWIP_HEAP_BYTES];
#else
#define T384_NET_HTTP_STORAGE
#define T384_NET_HEAP_STORAGE
#define T384_NET_NCM_STORAGE
#define T384_NET_SCRATCH_STORAGE
#define T384_NET_CONTROL_STORAGE
#endif

#endif
