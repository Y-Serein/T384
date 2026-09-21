#ifndef T384_RAW16_WIRE_H
#define T384_RAW16_WIRE_H

#include <stdint.h>

#include "t384_frame_pipeline.h"

#define T384_RAW16_WIRE_MAGIC 0x31523354u /* bytes: T3R1 */
#define T384_RAW16_WIRE_VERSION (T384_PIPELINE_PACKED_PICTURE ? 2u : 1u)
#define T384_RAW16_WIRE_HEADER_BYTES 36u
#define T384_FRAME_PIXEL_FORMAT_Y16_BE 2u
#define T384_FRAME_PIXEL_FORMAT_UYVY 3u
#define T384_FRAME_PIXEL_FORMAT_PACKED_UYVY 4u

uint16_t t384_frame_pixel_format_from_flags(uint16_t flags);
const char *t384_frame_pixel_format_name(uint16_t pixel_format);

uint16_t t384_raw16_crc16_xmodem(const uint8_t *data, uint16_t length);
void t384_raw16_wire_encode(uint8_t output[T384_RAW16_WIRE_HEADER_BYTES],
                            const t384_frame_chunk_view_t *chunk);

#endif
