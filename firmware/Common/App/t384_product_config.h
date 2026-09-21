#ifndef T384_PRODUCT_CONFIG_H
#define T384_PRODUCT_CONFIG_H

#include "t384_raw16.h"
#include "t384_frame_pipeline.h"

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
#define T384_NCM_IPV4_C 17u
#define T384_NCM_DEVICE_HOST 1u
#define T384_NCM_FIRST_CLIENT_HOST 2u
#define T384_NCM_CLIENT_COUNT 19u
#define T384_NCM_DOMAIN "ir.sipeed.com"

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
#if T384_RAW16_PROFILE == 640u
/* MINI2 UART table V0.4: 0x86 requests four bytes (last byte reserved).
 * WN2384's independently validated SDK path requests three. */
#define T384_MINI2_DIGITAL_STATE_BYTES 4u
#else
#define T384_MINI2_DIGITAL_STATE_BYTES 3u
#endif
#if T384_RAW16_PROFILE == 384u
/* WN2384/FW 00.00.07.01: captured DMA ROI/prefix is low byte first.
 * This describes receiver memory, not an unmeasured physical bus byte order.
 * Normalize at the source; the existing Y16BE wire contract stays unchanged.
 * WN2256's independently validated byte order must not inherit this setting. */
#define T384_MINI2_DVP_Y16_LITTLE_ENDIAN 1u
/* WCH RM V1.6 29.3.1: JPEG receive mode makes COL_NUM the DMA block
 * length. This is byte packing only, not a JPEG encoder or wire format. */
#define T384_MINI2_DMA_BLOCK_ROWS T384_PIPELINE_CHUNK_ROWS
#elif T384_RAW16_PROFILE == 640u
/* Independent bring-up hypothesis, not WN2384 byte-order evidence. Compare
 * /diag's raw prefix and both ROI interpretations before radiometry work. */
#ifndef T384_MINI2_DVP_Y16_LITTLE_ENDIAN
#define T384_MINI2_DVP_Y16_LITTLE_ENDIAN 0u
#endif
#define T384_MINI2_DMA_BLOCK_ROWS T384_PIPELINE_CHUNK_ROWS
#else
#define T384_MINI2_DVP_Y16_LITTLE_ENDIAN 0u
#define T384_MINI2_DMA_BLOCK_ROWS 1u
#endif
#define T384_MINI2_DMA_BLOCK_BYTES \
    (T384_MINI2_DVP_ROW_BYTES * T384_MINI2_DMA_BLOCK_ROWS)
#define T384_MINI2_CAPTURE_IDLE_MS 500u
/* Fault-only polling policy, not an OEM apply-time guarantee. */
#define T384_MINI2_MODULE_REARM_MS 4000u
#if T384_RAW16_PROFILE == 256u
#define T384_MINI2_DETECTOR_FPS 50u
#define T384_MINI2_DVP_FPS 50u
/* WN2256 engineering-only 0/50 C blackbody fit (1 cm, emissivity 0.98).
 * This is intentionally distinct from the fail-closed OEM radiometry path. */
#define T384_EXPERIMENTAL_TEMP_MODEL "experimental-blackbody-2point-v1"
#define T384_EXPERIMENTAL_Y16_ZERO_C_X100 3865997u
#define T384_EXPERIMENTAL_Y16_COUNTS_PER_C_X100 11828u
#elif T384_RAW16_PROFILE == 640u
/* TIFSC640/FW 01.00.01.03: logs show native ~60 FPS complete DVP frames.
 * This is the expected capture cadence, not an applied UART FPS command or
 * a claim about complete-frame HTTP throughput. No FPS setter is sent. */
#define T384_MINI2_DETECTOR_FPS 0u
#define T384_MINI2_DVP_FPS 60u
#define T384_EXPERIMENTAL_TEMP_MODEL "unavailable"
#define T384_EXPERIMENTAL_Y16_ZERO_C_X100 0u
#define T384_EXPERIMENTAL_Y16_COUNTS_PER_C_X100 0u
#else
/* Detector cadence and digital output cadence are independent commands.
 * Request native 60 FPS DVP; sustained complete-frame transport still needs
 * hardware validation. A setter ACK alone never enables collection. */
#define T384_MINI2_DETECTOR_FPS 60u
#define T384_MINI2_DVP_FPS 60u
#define T384_EXPERIMENTAL_TEMP_MODEL "unavailable"
#define T384_EXPERIMENTAL_Y16_ZERO_C_X100 0u
#define T384_EXPERIMENTAL_Y16_COUNTS_PER_C_X100 0u
#endif
/* Conservative bring-up deadlines, not OEM timing guarantees.  Setters may
 * reconfigure the video path; read-back queries remain bounded and shorter. */
#define T384_MINI2_QUERY_TIMEOUT_MS 250u
#define T384_MINI2_SET_TIMEOUT_MS 1000u
#define T384_MINI2_DETECTOR_SET_TIMEOUT_MS 2000u
#define T384_MINI2_QUERY_ATTEMPTS 3u
/* Boot confirmation policy, not a claimed OEM reconfiguration time. */
#define T384_MINI2_STATE_CONFIRM_TIMEOUT_MS 2000u
#define T384_MINI2_STATE_POLL_MS 50u
#define T384_MINI2_CONTROL_SKIPPED 4u
#define T384_MINI2_DVP_PCLK_FALLING 0u
#define T384_MINI2_DVP_HSYNC_LOW 0u
#define T384_MINI2_DVP_VSYNC_HIGH 1u
#define T384_MINI2_DVP_TIMING_VALIDATED 0u

#endif
