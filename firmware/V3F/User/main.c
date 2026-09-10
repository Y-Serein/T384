#include "ch32h417.h"
#include "ch32h417_swpmi.h"
#include "debug.h"
#include "system_ch32h417.h"
#include "t384_frame_pipeline.h"
#include "t384_frame_source.h"
#include "t384_ncm.h"
#include "t384_time.h"
#include "tusb.h"

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

    /* Match the USBHS pin-release sequence proven by Petros_DVP. */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SWPMI, ENABLE);
    SWPMI_BypassCmd(ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

    t384_ncm_prepare_identity();

    t384_time_init();
    t384_frame_pipeline_init();
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

    while (1) {
        tud_task();
        t384_frame_source_task();
        if (network_ready) {
            t384_ncm_task();
        }
    }
}
