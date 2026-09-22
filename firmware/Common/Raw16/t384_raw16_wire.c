#include "t384_raw16_wire.h"

#include <stddef.h>

static void put_le16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

uint16_t t384_raw16_crc16_xmodem(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0u;
    if (data == NULL) {
        return crc;
    }
    for (uint16_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u
                      ? (uint16_t)((crc << 1) ^ 0x1021u)
                      : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t t384_frame_pixel_format_from_flags(uint16_t flags)
{
    const uint16_t mode = flags & T384_CHUNK_FLAG_DATA_MODE_MASK;
    if (mode == T384_CHUNK_FLAG_TPD_Y16) {
        return T384_FRAME_PIXEL_FORMAT_Y16_BE;
    }
    if (mode == T384_CHUNK_FLAG_PICTURE_UYVY) {
        return (flags & T384_CHUNK_FLAG_PICTURE_PACKED) != 0u
            ? T384_FRAME_PIXEL_FORMAT_PACKED_UYVY : T384_FRAME_PIXEL_FORMAT_UYVY;
    }
    return 0u;
}

const char *t384_frame_pixel_format_name(uint16_t pixel_format)
{
    if (pixel_format == T384_FRAME_PIXEL_FORMAT_Y16_BE) {
        return "Y16BE";
    }
    if (pixel_format == T384_FRAME_PIXEL_FORMAT_UYVY) return "UYVY";
    return pixel_format == T384_FRAME_PIXEL_FORMAT_PACKED_UYVY
        ? "PACKED-UYVY" : "UNKNOWN";
}

void t384_raw16_wire_encode(uint8_t output[T384_RAW16_WIRE_HEADER_BYTES],
                            const t384_frame_chunk_view_t *chunk)
{
    if (output == NULL || chunk == NULL) {
        return;
    }
    put_le32(output + 0u, T384_RAW16_WIRE_MAGIC);
    put_le16(output + 4u, T384_RAW16_WIRE_VERSION);
    put_le16(output + 6u, T384_RAW16_WIRE_HEADER_BYTES);
    put_le32(output + 8u, chunk->frame_sequence);
    put_le32(output + 12u, chunk->frame_offset);
    put_le32(output + 16u, T384_PIPELINE_PACKED_PICTURE
        ? T384_PIPELINE_PACKED_FRAME_BYTES : T384_RAW16_FRAME_BYTES);
    put_le32(output + 20u, chunk->capture_ms);
    put_le16(output + 24u, chunk->length);
    put_le16(output + 26u, T384_RAW16_WIDTH);
    put_le16(output + 28u, T384_RAW16_HEIGHT);
    put_le16(output + 30u, chunk->flags);
    put_le16(output + 32u, t384_frame_pixel_format_from_flags(chunk->flags));
    put_le16(output + 34u, t384_raw16_crc16_xmodem(output, 34u));
}
