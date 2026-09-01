#ifndef T384_RAW16_WIRE_H
#define T384_RAW16_WIRE_H

#include <stdint.h>

#include "t384_frame_pipeline.h"

#define T384_RAW16_WIRE_MAGIC 0x31523354u /* bytes: T3R1 */
#define T384_RAW16_WIRE_VERSION 1u
#define T384_RAW16_WIRE_HEADER_BYTES 36u
#define T384_RAW16_PIXEL_FORMAT_LE16 1u

uint16_t t384_raw16_crc16_xmodem(const uint8_t *data, uint16_t length);
void t384_raw16_wire_encode(uint8_t output[T384_RAW16_WIRE_HEADER_BYTES],
                            const t384_frame_chunk_view_t *chunk);

#endif
