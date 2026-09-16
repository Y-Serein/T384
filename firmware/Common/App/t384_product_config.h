#ifndef T384_PRODUCT_CONFIG_H
#define T384_PRODUCT_CONFIG_H

#include "t384_raw16.h"

/*
 * Development USB identity.
 *
 * This is an unallocated, local-only test identity under the WCH VID. The PID
 * intentionally differs from both the WCH UVC reference and the CH372/CH375
 * diagnostic so Windows cannot reuse either cached driver binding. Override
 * both macros with an allocated product identity before distributing firmware.
 */
#ifndef T384_USB_VID
#define T384_USB_VID 0x1A86u
#endif

#ifndef T384_USB_PID
#define T384_USB_PID 0xE384u
#endif

#define T384_USB_BCD_DEVICE 0x0100u

#define T384_NCM_IPV4_A 192u
#define T384_NCM_IPV4_B 168u
#define T384_NCM_IPV4_C 18u
#define T384_NCM_DEVICE_HOST 1u
#define T384_NCM_FIRST_CLIENT_HOST 2u
#define T384_NCM_CLIENT_COUNT 3u

#define T384_HTTP_PORT 80u

/*
 * MINI2 DVP bring-up hypothesis.  The interface documents do not freeze the
 * sampling edge or sync polarity, so these values are deliberately exposed
 * in /diag and remain unvalidated until checked on the final board.
 */
#define T384_MINI2_DVP_WIDTH T384_RAW16_WIDTH
#define T384_MINI2_DVP_HEIGHT T384_RAW16_HEIGHT
#define T384_MINI2_DVP_ROW_BYTES (T384_MINI2_DVP_WIDTH * 2u)
#define T384_MINI2_DVP_EXPECTED_ROWS T384_MINI2_DVP_HEIGHT
#if T384_RAW16_PROFILE == 256u
#define T384_MINI2_DVP_FPS 50u
/* WN2256 engineering-only 0/50 C blackbody fit (1 cm, emissivity 0.98).
 * This is intentionally distinct from the fail-closed OEM radiometry path. */
#define T384_EXPERIMENTAL_TEMP_MODEL "experimental-blackbody-2point-v1"
#define T384_EXPERIMENTAL_Y16_ZERO_C_X100 3865997u
#define T384_EXPERIMENTAL_Y16_COUNTS_PER_C_X100 11828u
#else
#define T384_MINI2_DVP_FPS 30u
#define T384_EXPERIMENTAL_TEMP_MODEL "unavailable"
#define T384_EXPERIMENTAL_Y16_ZERO_C_X100 0u
#define T384_EXPERIMENTAL_Y16_COUNTS_PER_C_X100 0u
#endif
#define T384_MINI2_DVP_PCLK_FALLING 0u
#define T384_MINI2_DVP_HSYNC_LOW 0u
#define T384_MINI2_DVP_VSYNC_HIGH 1u
#define T384_MINI2_DVP_TIMING_VALIDATED 0u

#endif
