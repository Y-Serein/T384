#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "t384_raw16_wire.h"

#define BUF_LEN 2592u

static uint32_t lcg = 0x12345678u;
static uint32_t next_u32(void)
{
    lcg = lcg * 1664525u + 1013904223u;
    return lcg;
}

static void check_roundtrip(const uint8_t *input, uint16_t len,
                            const char *label)
{
    uint8_t enc[BUF_LEN + 8u];
    uint8_t dec[BUF_LEN];
    uint16_t e = t384_raw16_delta_rle_encode(input, len, enc, len);

    if (e == 0u) {
        /* Fallback is allowed: codec must not be required to compress. */
        return;
    }
    assert(e <= len);
    memset(dec, 0, sizeof(dec));
    t384_raw16_delta_rle_decode(enc, e, dec, len);
    assert(memcmp(dec, input, len) == 0);
    (void)label;
}

int main(void)
{
    uint8_t input[BUF_LEN];

    /* 1. Smooth ramp: every byte equals its predecessor. */
    memset(input, 0x5A, sizeof(input));
    check_roundtrip(input, BUF_LEN, "flat");

    /* 2. Smooth with occasional +/-1 jitter, like real thermal data. */
    {
        uint16_t i;
        input[0] = 100;
        for (i = 1u; i < BUF_LEN; i++) {
            uint32_t r = next_u32() & 7u;
            if (r == 0u) {
                input[i] = (uint8_t)(input[i - 1u] + 1u);
            } else if (r == 1u) {
                input[i] = (uint8_t)(input[i - 1u] - 1u);
            } else {
                input[i] = input[i - 1u];
            }
        }
    }
    check_roundtrip(input, BUF_LEN, "jitter");
    {
        uint16_t e = t384_raw16_delta_rle_encode(input, BUF_LEN,
                                                 (uint8_t[BUF_LEN]){0}, BUF_LEN);
        assert(e > 0u && e < BUF_LEN / 2u);
    }

    /* 3. Incompressible random data: must return 0 (fallback). */
    {
        uint16_t i;
        for (i = 0u; i < BUF_LEN; i++) {
            input[i] = (uint8_t)next_u32();
        }
    }
    {
        uint16_t e = t384_raw16_delta_rle_encode(input, BUF_LEN,
                                                 (uint8_t[BUF_LEN]){0}, BUF_LEN);
        assert(e == 0u);
    }

    /* 4. Run longer than 255: must split into multiple runs and round-trip. */
    memset(input, 7u, sizeof(input));
    check_roundtrip(input, BUF_LEN, "long-run");

    /* 5. Byte wraparound edge: 255 -> 0 -> 255. */
    input[0] = 255;
    input[1] = 0;
    input[2] = 255;
    input[3] = 1;
    input[4] = 1;
    check_roundtrip(input, 5u, "wrap");

    /* 6. Minimal lengths: 0 and 1 bytes cannot be encoded (return 0). */
    assert(t384_raw16_delta_rle_encode(input, 0u, (uint8_t[BUF_LEN]){0}, BUF_LEN) == 0u);
    assert(t384_raw16_delta_rle_encode(input, 1u, (uint8_t[BUF_LEN]){0}, BUF_LEN) == 0u);
    /* 2 bytes: one absolute + one run pair. */
    {
        uint8_t two[2] = {10, 10};
        uint8_t out[4];
        uint16_t e = t384_raw16_delta_rle_encode(two, 2u, out, sizeof(out));
        assert(e == 3u);
        assert(out[1] == 1u && out[2] == 0u);
    }

    printf("t384_raw16_delta smoke: OK\n");
    return 0;
}
