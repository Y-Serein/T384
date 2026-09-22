#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "t384_picture_rice.h"

static void check_block(const uint8_t *y, uint8_t expected_codec)
{
    uint8_t packed[T384_PICTURE_RICE_BLOCK_BYTES] = {0};
    uint8_t encoded[T384_PICTURE_RICE_BLOCK_BYTES] = {0};
    uint8_t decoded[T384_PICTURE_RICE_BLOCK_BYTES] = {0};
    packed[0] = 0x31u;
    packed[1] = 0x52u;
    packed[2] = 0x71u;
    packed[3] = 0x93u;
    memcpy(packed + T384_PICTURE_RICE_PREFIX_BYTES, y,
           T384_PICTURE_RICE_Y_BYTES);

    t384_picture_rice_block_info_t info;
    assert(t384_picture_rice_encode_block(encoded, packed, &info));
    assert(info.codec == expected_codec);
    assert(info.encoded_bytes <= T384_PICTURE_RICE_BLOCK_BYTES);
    assert(t384_picture_rice_decode_block(decoded, encoded, info.encoded_bytes));
    assert(memcmp(decoded, packed, 4u) == 0);
    assert(memcmp(decoded + T384_PICTURE_RICE_PREFIX_BYTES,
                  packed + T384_PICTURE_RICE_PREFIX_BYTES,
                  T384_PICTURE_RICE_Y_BYTES) == 0);
}

int main(void)
{
    uint8_t smooth[T384_PICTURE_RICE_Y_BYTES];
    uint8_t noisy[T384_PICTURE_RICE_Y_BYTES];
    uint32_t state = 0x12345678u;
    memset(smooth, 80, sizeof(smooth));
    for (size_t i = 0; i < sizeof(noisy); ++i) {
        state = state * 1664525u + 1013904223u;
        noisy[i] = (uint8_t)(state >> 24);
    }
    check_block(smooth, T384_PICTURE_RICE_CODEC_RICE);
    check_block(noisy, T384_PICTURE_RICE_CODEC_RAW);
    puts("640 Picture Rice/raw fallback C codec smoke passed");
    return 0;
}
