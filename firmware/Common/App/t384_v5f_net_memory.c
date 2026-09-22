#include "t384_v5f_net_memory.h"

#if T384_NETWORK_ON_V5F && defined(Core_V5F)
uint8_t t384_v5f_lwip_heap[T384_V5F_LWIP_HEAP_BYTES]
    T384_NET_HEAP_STORAGE;
#endif
