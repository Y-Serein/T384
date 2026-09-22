#include "ch32h417.h"
#include "ch32h417_swpmi.h"
#include "debug.h"
#include "system_ch32h417.h"
#include "t384_frame_pipeline.h"
#include "t384_frame_source.h"
#include "t384_dualcore.h"
#if !T384_NETWORK_ON_V5F
#include "t384_ncm.h"
#include "tusb.h"
#endif
#include "t384_time.h"

#if !defined(T384_HOST_SYNTAX_CHECK)
#if !defined(__OPTIMIZE__) || defined(__OPTIMIZE_SIZE__)
#error "T384 RAW16 bench requires effective -O2; check MRS optimization flag order"
#endif
#endif

int main(void)
{
    SystemInit();
    SystemAndCoreClockUpdate();

    /* Match the proven Petros_DVP V3F bring-up window and debug UART. */
    Delay_Init();
    USART_Printf_Init(115200u);
    Delay_Ms(1000u);
    printf("T384 product firmware V3F boot, SystemClk:%lu CoreClk:%lu\r\n",
           (unsigned long)SystemClock, (unsigned long)SystemCoreClock);
    Delay_Ms(500u);

    /* Keep the USBHS pin-release sequence on the network owner. */
#if !T384_NETWORK_ON_V5F
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SWPMI, ENABLE);
    SWPMI_BypassCmd(ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);
#endif

#if T384_DUALCORE
    t384_frame_pipeline_init();     /* frame pipeline */
    t384_time_init();
#if T384_NETWORK_ON_V5F
    printf("RAW16 dual-core V5F capture + network data plane\r\n");
#else
    t384_ncm_prepare_identity();
    printf("RAW16 dual-core V5F capture @0x30000, V3F network\r\n");
#endif
#else
    t384_ncm_prepare_identity();
    t384_time_init();
    t384_frame_pipeline_init();     /* frame pipeline */
    if (!t384_frame_source_init()) {
        printf("RAW16 capture adapter init failed\r\n");
        while (1) {
        }
    }
    printf("RAW16 source=%s, %ux%u, %lu bytes/frame\r\n",
           t384_frame_source_name(), T384_RAW16_WIDTH, T384_RAW16_HEIGHT,
           (unsigned long)T384_RAW16_FRAME_BYTES);
    if (!t384_frame_source_stream_ready()) {
        printf("MINI2 DVP diagnostic mode; RAW16 stream is gated\r\n");
    }

#endif

#if !T384_NETWORK_ON_V5F
    const tusb_rhport_init_t usb_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    if (!tusb_init(BOARD_TUD_RHPORT, &usb_init)) {
        printf("USBHS/TinyUSB init failed\r\n");
        while (1) {
        }
    }
    printf("USBHS/TinyUSB init passed\r\n");

    const bool network_ready = t384_ncm_init();
    printf(network_ready ? "NCM/lwIP init passed\r\n"
                         : "NCM/lwIP init failed; USB kept active\r\n");
#endif

#if T384_DUALCORE
    /* Wake after shared-memory initialization.  In V5F-network mode the
     * network stack is initialized by the awakened core itself. */
    NVIC_WakeUp_V5F(T384_V5F_ENTRY);
#endif

    while (1) {
#if !T384_NETWORK_ON_V5F
        tud_task();
        t384_frame_source_task();
        if (network_ready) {
            t384_ncm_task();
        }
#else
        t384_frame_source_task();
#endif
    }
}
