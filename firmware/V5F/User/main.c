#include "ch32h417.h"
#include "ch32h417_swpmi.h"
#include "system_ch32h417.h"
#include "t384_dualcore.h"
#include "t384_time.h"
#if T384_NETWORK_ON_V5F
#include "t384_ncm.h"
#include "tusb.h"
#endif
#include <string.h>

#if !T384_DUALCORE || !defined(Core_V5F)
#error "This main requires the V5F dual-core capture configuration"
#endif

int main(void)
{
    /* V3F still owns SystemInit/clocks.  V5F owns the high-rate network
     * data plane when the 640 architecture switch is enabled. */
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
#if T384_NETWORK_ON_V5F
    /* USBHS pin release belongs to the core that owns TinyUSB/DCD. */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SWPMI, ENABLE);
    SWPMI_BypassCmd(ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);
    t384_ncm_prepare_identity();
    const tusb_rhport_init_t usb_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    /* DVP/DMA remains the highest-priority data source; USBHS drains below
     * it and all lwIP/HTTP work stays in this main loop. */
    NVIC_SetPriority(USBHS_IRQn, 1u);
    if (!tusb_init(BOARD_TUD_RHPORT, &usb_init)) {
        while (1) { }
    }
    const bool network_ready = t384_ncm_init();
    while (1) {
        tud_task();
        /* This task owns MINI2 servicing, frame production and the shared
         * diagnostic/RPC snapshot.  Keep it adjacent to the network loop so
         * a leased chunk is consumed before the next capture burst. */
        t384_dualcore_capture_task();
        if (network_ready) t384_ncm_task();
    }
#else
    while (1) t384_dualcore_capture_task();
#endif
}
