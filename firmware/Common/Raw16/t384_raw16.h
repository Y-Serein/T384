#ifndef T384_RAW16_H
#define T384_RAW16_H

#include <stddef.h>
#include <stdint.h>

/* Change this single selector for a product build. */
#ifndef T384_RAW16_PROFILE
#define T384_RAW16_PROFILE 640u
#endif

#if T384_RAW16_PROFILE == 256u
#define T384_RAW16_WIDTH 256u
#define T384_RAW16_HEIGHT 192u
#elif T384_RAW16_PROFILE == 384u
#define T384_RAW16_WIDTH 384u
#define T384_RAW16_HEIGHT 288u
#elif T384_RAW16_PROFILE == 640u
#define T384_RAW16_WIDTH 640u
#define T384_RAW16_HEIGHT 512u
#else
#error "T384_RAW16_PROFILE must be 256u, 384u or 640u"
#endif
#define T384_RAW16_BYTES_PER_PIXEL 2u
#define T384_RAW16_PIXELS (T384_RAW16_WIDTH * T384_RAW16_HEIGHT)
#define T384_RAW16_FRAME_BYTES \
    (T384_RAW16_PIXELS * T384_RAW16_BYTES_PER_PIXEL)
#define T384_RAW16_PIXEL_BIG_ENDIAN 1u

/*
 * The simulator owns this deterministic scene pattern.  Frame identity and
 * transport metadata are carried by the chunk envelope, never by RAW pixels.
 * A real MINI2 source replaces the simulator and writes its bytes through the
 * same frame-pipeline producer API without changing downstream consumers.
 */
uint16_t t384_raw16_word(uint32_t frame_index, uint32_t word_index);
size_t t384_raw16_fill(uint32_t frame_index, uint32_t frame_offset,
                       uint8_t *destination, size_t length);

#endif
