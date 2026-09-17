#include "ch32h417.h"
#include "system_ch32h417.h"
#include "t384_dualcore.h"
#include "t384_time.h"
#include <string.h>

#if !T384_DUALCORE || !defined(Core_V5F)
#error "This main requires the V5F dual-core capture configuration"
#endif

int main(void)
{
    /* V3F owns SystemInit, clocks, debug UART, SysTick and USB/network. */
    SystemAndCoreClockUpdate();
    t384_time_init();
    t384_dualcore_shared_t *s = &t384_dualcore_shared;
    if (__atomic_load_n(&s->magic, __ATOMIC_ACQUIRE) != T384_IPC_MAGIC ||
        s->version != T384_IPC_VERSION) {
        while (1) { }
    }

    for (unsigned region = 0u; region < T384_FRAME_PROBE_REGIONS; ++region) {
        const uint32_t probe = T384_DTCM_PROBE + region;
        memcpy(t384_frame_probe_region(region), &probe, sizeof(probe));
    }
    __atomic_store_n(&s->v5f_booted, 1u, __ATOMIC_RELEASE);
    /* V3F polls the shared condition; unlike the demo it never enters STOP
     * and keeps servicing USB/network while V5F initializes the module. */
    const uint32_t started = t384_millis();
    while (__atomic_load_n(&s->v3f_frame_access_ok, __ATOMIC_ACQUIRE) == 0u &&
           (uint32_t)(t384_millis() - started) < 1000u) { }
    if (s->v3f_frame_access_ok != 1u) {
        while (1) { }
    }

    const bool initialized = t384_frame_source_init();
    __atomic_store_n(&s->v5f_initialized, initialized ? 1u : 0u, __ATOMIC_RELEASE);
    while (1) t384_dualcore_capture_task();
}
