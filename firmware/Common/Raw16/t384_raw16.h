#ifndef T384_RAW16_H
#define T384_RAW16_H

#include <stddef.h>
#include <stdint.h>

#define T384_RAW16_WIDTH 384u
#define T384_RAW16_HEIGHT 288u
#define T384_RAW16_BYTES_PER_PIXEL 2u
#define T384_RAW16_PIXELS (T384_RAW16_WIDTH * T384_RAW16_HEIGHT)
#define T384_RAW16_FRAME_BYTES \
    (T384_RAW16_PIXELS * T384_RAW16_BYTES_PER_PIXEL)

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
