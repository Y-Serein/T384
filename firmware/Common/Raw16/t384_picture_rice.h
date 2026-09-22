#ifndef T384_PICTURE_RICE_H
#define T384_PICTURE_RICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Experimental 640-only codec. The outer 32-byte Picture prefix is retained;
 * bytes 4..9 carry the experimental block metadata. V2 remains the default
 * wire format until the target-cycle and browser gates pass. */
#define T384_PICTURE_RICE_CODEC_RAW 0u
#define T384_PICTURE_RICE_CODEC_RICE 1u
#define T384_PICTURE_RICE_K 1u
#define T384_PICTURE_RICE_PREFIX_BYTES 32u
#define T384_PICTURE_RICE_Y_BYTES 2560u
#define T384_PICTURE_RICE_BLOCK_BYTES \
    (T384_PICTURE_RICE_PREFIX_BYTES + T384_PICTURE_RICE_Y_BYTES)

typedef struct {
    uint16_t encoded_bytes;
    uint8_t codec;
    uint8_t rice_k;
} t384_picture_rice_block_info_t;

static inline uint8_t t384_picture_rice_zigzag(uint8_t current, uint8_t previous)
{
    const int16_t delta = (int16_t)(uint8_t)(current - previous);
    const int16_t signed_delta = delta < 128 ? delta : (int16_t)(delta - 256);
    return signed_delta >= 0
        ? (uint8_t)(signed_delta << 1)
        : (uint8_t)((-signed_delta << 1) - 1);
}

static inline int16_t t384_picture_rice_unzigzag(uint8_t value)
{
    return (value & 1u) != 0u
        ? (int16_t)(-((int16_t)(value >> 1)) - 1)
        : (int16_t)(value >> 1);
}

static inline bool t384_picture_rice_put_bit(uint8_t *output,
                                             uint16_t *bit_offset,
                                             uint16_t bit_capacity,
                                             uint8_t bit)
{
    if (*bit_offset >= bit_capacity) return false;
    const uint16_t byte = (uint16_t)(*bit_offset >> 3);
    const uint8_t shift = (uint8_t)(7u - (*bit_offset & 7u));
    if (bit != 0u) output[byte] |= (uint8_t)(1u << shift);
    ++*bit_offset;
    return true;
}

static inline bool t384_picture_rice_get_bit(const uint8_t *input,
                                             uint16_t *bit_offset,
                                             uint16_t bit_capacity,
                                             uint8_t *bit)
{
    if (*bit_offset >= bit_capacity || bit == NULL) return false;
    *bit = (uint8_t)((input[*bit_offset >> 3] >>
                      (7u - (*bit_offset & 7u))) & 1u);
    ++*bit_offset;
    return true;
}

/* Encode one already-packed 2592-byte block. The source and destination may
 * not overlap; the V5F adapter will provide a separate raw staging view while
 * the host smoke uses ordinary buffers. */
static inline bool t384_picture_rice_encode_block(
    uint8_t *output, const uint8_t *packed,
    t384_picture_rice_block_info_t *info)
{
    if (output == NULL || packed == NULL || info == NULL) return false;
    memcpy(output, packed, T384_PICTURE_RICE_PREFIX_BYTES);
    memset(output + 4u, 0, 28u);
    output[4] = T384_PICTURE_RICE_CODEC_RICE;
    output[5] = T384_PICTURE_RICE_K;
    output[6] = (uint8_t)(T384_PICTURE_RICE_Y_BYTES & 0xffu);
    output[7] = (uint8_t)(T384_PICTURE_RICE_Y_BYTES >> 8);
    output[8] = 0u;
    output[9] = 0u;

    uint8_t *body = output + T384_PICTURE_RICE_PREFIX_BYTES;
    memset(body, 0, T384_PICTURE_RICE_Y_BYTES);
    uint16_t bits = 0u;
    const uint16_t capacity = (uint16_t)(T384_PICTURE_RICE_Y_BYTES * 8u);
    const uint8_t *y = packed + T384_PICTURE_RICE_PREFIX_BYTES;
    uint8_t previous = y[0];
    body[0] = previous;
    bits = 8u;
    bool overflow = false;
    for (uint32_t index = 1u; index < T384_PICTURE_RICE_Y_BYTES && !overflow;
         ++index) {
        const uint8_t value = y[index];
        const uint8_t symbol = t384_picture_rice_zigzag(value, previous);
        previous = value;
        const uint16_t quotient = (uint16_t)(symbol >> T384_PICTURE_RICE_K);
        for (uint16_t zero = 0u; zero < quotient; ++zero) {
            if (!t384_picture_rice_put_bit(body, &bits, capacity, 0u)) {
                overflow = true;
                break;
            }
        }
        if (!overflow && !t384_picture_rice_put_bit(body, &bits, capacity, 1u))
            overflow = true;
        if (!overflow && !t384_picture_rice_put_bit(
                body, &bits, capacity,
                (uint8_t)(symbol & ((1u << T384_PICTURE_RICE_K) - 1u))))
            overflow = true;
    }

    if (overflow) {
        memcpy(output, packed, T384_PICTURE_RICE_BLOCK_BYTES);
        memset(output + 4u, 0, 28u);
        output[4] = T384_PICTURE_RICE_CODEC_RAW;
        output[6] = (uint8_t)(T384_PICTURE_RICE_Y_BYTES & 0xffu);
        output[7] = (uint8_t)(T384_PICTURE_RICE_Y_BYTES >> 8);
        output[8] = (uint8_t)(T384_PICTURE_RICE_Y_BYTES & 0xffu);
        output[9] = (uint8_t)(T384_PICTURE_RICE_Y_BYTES >> 8);
        info->encoded_bytes = T384_PICTURE_RICE_BLOCK_BYTES;
        info->codec = T384_PICTURE_RICE_CODEC_RAW;
        info->rice_k = 0u;
        return true;
    }

    const uint16_t body_bytes = (uint16_t)((bits + 7u) / 8u);
    const uint16_t total = (uint16_t)(T384_PICTURE_RICE_PREFIX_BYTES + body_bytes);
    if (total >= T384_PICTURE_RICE_BLOCK_BYTES) {
        memcpy(output, packed, T384_PICTURE_RICE_BLOCK_BYTES);
        memset(output + 4u, 0, 28u);
        output[4] = T384_PICTURE_RICE_CODEC_RAW;
        output[6] = (uint8_t)(T384_PICTURE_RICE_Y_BYTES & 0xffu);
        output[7] = (uint8_t)(T384_PICTURE_RICE_Y_BYTES >> 8);
        output[8] = (uint8_t)(T384_PICTURE_RICE_Y_BYTES & 0xffu);
        output[9] = (uint8_t)(T384_PICTURE_RICE_Y_BYTES >> 8);
        info->encoded_bytes = T384_PICTURE_RICE_BLOCK_BYTES;
        info->codec = T384_PICTURE_RICE_CODEC_RAW;
        info->rice_k = 0u;
        return true;
    }
    output[8] = (uint8_t)(body_bytes & 0xffu);
    output[9] = (uint8_t)(body_bytes >> 8);
    info->encoded_bytes = total;
    info->codec = T384_PICTURE_RICE_CODEC_RICE;
    info->rice_k = T384_PICTURE_RICE_K;
    return true;
}

static inline bool t384_picture_rice_decode_block(
    uint8_t *output, const uint8_t *encoded, uint16_t encoded_bytes)
{
    if (output == NULL || encoded == NULL ||
        encoded_bytes < T384_PICTURE_RICE_PREFIX_BYTES + 1u ||
        encoded_bytes > T384_PICTURE_RICE_BLOCK_BYTES) return false;
    memcpy(output, encoded, T384_PICTURE_RICE_PREFIX_BYTES);
    const uint8_t codec = encoded[4];
    const uint16_t decoded_bytes = (uint16_t)encoded[6] |
                                   ((uint16_t)encoded[7] << 8);
    if (decoded_bytes != T384_PICTURE_RICE_Y_BYTES) return false;
    if (codec == T384_PICTURE_RICE_CODEC_RAW) {
        if (encoded_bytes != T384_PICTURE_RICE_BLOCK_BYTES) return false;
        memcpy(output + T384_PICTURE_RICE_PREFIX_BYTES,
               encoded + T384_PICTURE_RICE_PREFIX_BYTES,
               T384_PICTURE_RICE_Y_BYTES);
        return true;
    }
    if (codec != T384_PICTURE_RICE_CODEC_RICE ||
        encoded[5] != T384_PICTURE_RICE_K) return false;
    const uint16_t body_bytes = (uint16_t)encoded[8] |
                                ((uint16_t)encoded[9] << 8);
    if ((uint16_t)(body_bytes + T384_PICTURE_RICE_PREFIX_BYTES) != encoded_bytes ||
        body_bytes == 0u) return false;
    const uint8_t *body = encoded + T384_PICTURE_RICE_PREFIX_BYTES;
    uint8_t *y = output + T384_PICTURE_RICE_PREFIX_BYTES;
    y[0] = body[0];
    uint16_t bits = 8u;
    const uint16_t capacity = (uint16_t)(body_bytes * 8u);
    for (uint32_t index = 1u; index < T384_PICTURE_RICE_Y_BYTES; ++index) {
        uint16_t quotient = 0u;
        uint8_t bit = 0u;
        do {
            if (!t384_picture_rice_get_bit(body, &bits, capacity, &bit)) return false;
            if (bit == 0u) {
                if (quotient == UINT16_MAX) return false;
                ++quotient;
            }
        } while (bit == 0u);
        uint8_t remainder = 0u;
        if (!t384_picture_rice_get_bit(body, &bits, capacity, &remainder)) return false;
        const uint8_t symbol = (uint8_t)((quotient << T384_PICTURE_RICE_K) | remainder);
        const int16_t delta = t384_picture_rice_unzigzag(symbol);
        y[index] = (uint8_t)(y[index - 1u] + delta);
    }
    return true;
}

#endif
