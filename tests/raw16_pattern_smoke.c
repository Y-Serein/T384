#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "t384_raw16.h"

static uint8_t frame[T384_RAW16_FRAME_BYTES];

int main(void)
{
    static const size_t chunks[] = {1u, 7u, 1459u, 1460u, 4095u};
    const uint32_t frame_index = 0x12345678u;
    size_t offset = 0u;
    size_t chunk_index = 0u;

    assert(T384_RAW16_FRAME_BYTES ==
           T384_RAW16_WIDTH * T384_RAW16_HEIGHT * 2u);
    assert(t384_raw16_word(frame_index, 0u) == 0x0348u);
    assert(t384_raw16_word(frame_index, 1u) == 0x0449u);
    assert(t384_raw16_word(frame_index, 7u) ==
           (uint16_t)(7u * 257u + 0x0348u));
    assert(t384_raw16_word(frame_index + 1u, 0u) ==
           t384_raw16_word(frame_index, 0u));

    while (offset < sizeof(frame)) {
        size_t length = chunks[chunk_index++ %
                               (sizeof(chunks) / sizeof(chunks[0]))];
        if (length > sizeof(frame) - offset) {
            length = sizeof(frame) - offset;
        }
        assert(t384_raw16_fill(frame_index, (uint32_t)offset,
                               frame + offset, length) == length);
        offset += length;
    }

    for (uint32_t word = 0u; word < T384_RAW16_PIXELS; ++word) {
        const uint16_t actual =
            (uint16_t)((uint16_t)frame[word * 2u] |
                       ((uint16_t)frame[word * 2u + 1u] << 8));
        assert(actual == t384_raw16_word(frame_index, word));
    }

    assert(t384_raw16_fill(frame_index, T384_RAW16_FRAME_BYTES,
                           frame, 1u) == 0u);
    assert(t384_raw16_fill(frame_index, 0u, NULL, 1u) == 0u);
    puts("T384 RAW16 pure-pixel pattern smoke passed");
    return 0;
}
