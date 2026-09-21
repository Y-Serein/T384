#ifndef T384_PACKED_PICTURE_H
#define T384_PACKED_PICTURE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Internal lossless Picture storage. All cross-core accesses are aligned
 * words. Preserve actual block U/V; refuse varying chroma, never approximate. */
static inline uint32_t t384_picture_uyvy_word(uint32_t w, uint32_t format)
{
    if (format == 1u) return (w & 0xFF00FF00u) | ((w & 255u) << 16) | ((w >> 16) & 255u);
    if (format == 2u) return ((w & 0x00FF00FFu) << 8) | ((w & 0xFF00FF00u) >> 8);
    if (format == 3u) return (w << 8) | (w >> 24);
    return w;
}
static inline bool t384_picture_pack(uint8_t *out, const uint8_t *in,
                                     uint32_t bytes, uint32_t format)
{
    if (format > 3u || !bytes || bytes % 8u) return false;
    out = __builtin_assume_aligned(out, 4u);
    in = __builtin_assume_aligned(in, 4u);
    uint32_t first;
    memcpy(&first, in, 4u);
    const uint32_t uv = t384_picture_uyvy_word(first, format) & 0x00FF00FFu;
    memcpy(out, &uv, 4u);
    for (uint32_t i = 0u; i < bytes; i += 8u) {
        uint32_t a, b;
        memcpy(&a, in + i, 4u); memcpy(&b, in + i + 4u, 4u);
        a = t384_picture_uyvy_word(a, format); b = t384_picture_uyvy_word(b, format);
        if ((a & 0x00FF00FFu) != uv || (b & 0x00FF00FFu) != uv) return false;
        const uint32_t y = ((a >> 8) & 255u) | ((a >> 16) & 0xFF00u) |
                           ((b << 8) & 0xFF0000u) | (b & 0xFF000000u);
        memcpy(out + 32u + i / 2u, &y, 4u);
    }
    return true;
}
static inline void t384_picture_expand(uint8_t *out, const uint8_t *in, uint32_t bytes)
{
    out = __builtin_assume_aligned(out, 4u);
    in = __builtin_assume_aligned(in, 4u);
    uint32_t uv;
    memcpy(&uv, in, 4u);
    for (uint32_t i = 0u; i < bytes; i += 8u) {
        uint32_t y;
        memcpy(&y, in + 32u + i / 2u, 4u);
        const uint32_t a = uv | ((y & 255u) << 8) | ((y & 0xFF00u) << 16);
        const uint32_t b = uv | ((y >> 8) & 0xFF00u) | (y & 0xFF000000u);
        memcpy(out + i, &a, 4u); memcpy(out + i + 4u, &b, 4u);
    }
}
#endif
