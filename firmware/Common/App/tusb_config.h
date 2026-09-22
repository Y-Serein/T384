#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#include "t384_raw16.h"

#define CFG_TUSB_MCU          OPT_MCU_CH32H417
#define BOARD_TUD_RHPORT      0
#define BOARD_TUD_MAX_SPEED   OPT_MODE_HIGH_SPEED
#define CFG_TUD_MAX_SPEED     OPT_MODE_HIGH_SPEED
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)

#define CFG_TUSB_OS           OPT_OS_NONE
#define CFG_TUSB_DEBUG        0
#define CFG_TUD_ENABLED       1

#if T384_NETWORK_ON_V5F && defined(Core_V5F)
#define CFG_TUSB_MEM_SECTION \
    __attribute__((section(".t384_net_ncm"), aligned(32)))
#else
#define CFG_TUSB_MEM_SECTION
#endif
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_AUDIO     0
#define CFG_TUD_CDC       0
#define CFG_TUD_DFU       0
#define CFG_TUD_DFU_RUNTIME 0
#define CFG_TUD_HID       0
#define CFG_TUD_MIDI      0
#define CFG_TUD_MSC       0
#define CFG_TUD_VENDOR    0
#define CFG_TUD_VIDEO     0
#define CFG_TUD_ECM_RNDIS 0
#define CFG_TUD_NCM       1

#define CFG_TUD_NET_MTU 1514

/* Keep several larger HS NTBs in flight so continuous RAW16 does not stall on
 * a 4 KiB USB transfer window. The buffers are bounded and map-checked. */
#define CFG_TUD_NCM_IN_NTB_MAX_SIZE  16384
#define CFG_TUD_NCM_OUT_NTB_MAX_SIZE 4096
#if T384_NETWORK_ON_V5F && defined(Core_V5F)
/* Two 16 KiB NTBs fit the dedicated V5F shared-code bank. */
#define CFG_TUD_NCM_IN_NTB_N         2
#else
#define CFG_TUD_NCM_IN_NTB_N         3
#endif
#define CFG_TUD_NCM_OUT_NTB_N        1
#define CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB  16
#define CFG_TUD_NCM_OUT_MAX_DATAGRAMS_PER_NTB 6

#endif
