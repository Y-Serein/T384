#include "t384_raw16.h"

#if T384_RAW16_FRAME_BYTES != 98304u && T384_RAW16_FRAME_BYTES != 221184u
#error "unsupported T384 RAW16 frame size"
#endif

uint16_t t384_raw16_word(uint32_t frame_index, uint32_t word_index)
{
    (void)frame_index;
    return (uint16_t)(word_index * 257u + 0x0348u);
}

size_t t384_raw16_fill(uint32_t frame_index, uint32_t frame_offset,
                       uint8_t *destination, size_t length)
{
    if (destination == NULL || frame_offset >= T384_RAW16_FRAME_BYTES) {
        return 0u;
    }

    const size_t available = T384_RAW16_FRAME_BYTES - frame_offset;
    if (length > available) {
        length = available;
    }
    const size_t written = length;
    uint32_t word_index = frame_offset / 2u;

    if ((frame_offset & 1u) != 0u && length != 0u) {
        *destination++ = (uint8_t)t384_raw16_word(frame_index, word_index);
        --length;
        ++word_index;
    }

    if (length >= 2u) {
        uint16_t value = t384_raw16_word(frame_index, word_index);
        while (length >= 2u) {
            destination[0] = (uint8_t)(value >> 8);
            destination[1] = (uint8_t)value;
            destination += 2;
            length -= 2u;
            ++word_index;
            value = (uint16_t)(value + 257u);
        }
    }
    if (length != 0u) {
        *destination =
            (uint8_t)(t384_raw16_word(frame_index, word_index) >> 8);
    }
    return written;
}
