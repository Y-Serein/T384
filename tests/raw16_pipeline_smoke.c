#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "t384_frame_pipeline.h"
#include "t384_raw16.h"
#include "t384_raw16_wire.h"

static uint16_t get_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t get_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void produce_chunk(uint32_t sequence, uint32_t offset, bool end)
{
    uint8_t *data = NULL;
    uint16_t capacity = 0u;
    assert(t384_frame_pipeline_acquire_write(&data, &capacity));
    assert(capacity == T384_PIPELINE_CHUNK_BYTES);
    assert(t384_raw16_fill(sequence, offset, data, capacity) == capacity);
    assert(t384_frame_pipeline_commit_write(capacity, end));
}

int main(void)
{
    const uint32_t sequence = 0x12345678u;
    t384_frame_pipeline_init();
    assert(t384_frame_pipeline_begin_frame(sequence, 4321u,
                                           T384_CHUNK_FLAG_SYNTHETIC |
                                               T384_CHUNK_FLAG_TPD_Y16));

    for (uint32_t offset = 0u; offset < T384_RAW16_FRAME_BYTES;
         offset += T384_PIPELINE_CHUNK_BYTES) {
        const bool end = offset + T384_PIPELINE_CHUNK_BYTES ==
                         T384_RAW16_FRAME_BYTES;
        produce_chunk(sequence, offset, end);

        t384_frame_chunk_view_t chunk;
        assert(t384_frame_pipeline_peek(&chunk));
        assert(chunk.frame_sequence == sequence);
        assert(chunk.frame_offset == offset);
        assert(chunk.length == T384_PIPELINE_CHUNK_BYTES);
        assert((chunk.flags & T384_CHUNK_FLAG_SYNTHETIC) != 0u);
        assert((chunk.flags & T384_CHUNK_FLAG_DATA_MODE_MASK) ==
               T384_CHUNK_FLAG_TPD_Y16);
        assert(((chunk.flags & T384_CHUNK_FLAG_FRAME_START) != 0u) ==
               (offset == 0u));
        assert(((chunk.flags & T384_CHUNK_FLAG_FRAME_END) != 0u) == end);
        assert(chunk.data[0] ==
               (uint8_t)(t384_raw16_word(sequence, offset / 2u) >> 8));

        uint8_t header[T384_RAW16_WIRE_HEADER_BYTES];
        t384_raw16_wire_encode(header, &chunk);
        assert(get_le32(header) == T384_RAW16_WIRE_MAGIC);
        assert(get_le16(header + 4u) == T384_RAW16_WIRE_VERSION);
        assert(get_le16(header + 6u) == T384_RAW16_WIRE_HEADER_BYTES);
        assert(get_le32(header + 8u) == sequence);
        assert(get_le32(header + 12u) == offset);
        assert(get_le32(header + 16u) == T384_RAW16_FRAME_BYTES);
        assert(get_le16(header + 24u) == T384_PIPELINE_CHUNK_BYTES);
        assert(get_le16(header + 32u) == T384_FRAME_PIXEL_FORMAT_Y16_BE);
        assert(get_le16(header + 34u) ==
               t384_raw16_crc16_xmodem(header, 34u));
        t384_frame_pipeline_release();
    }

    t384_frame_pipeline_stats_t stats;
    t384_frame_pipeline_get_stats(&stats);
    assert(stats.frames_started == 1u);
    assert(stats.frames_completed == 1u);
    assert(stats.frames_aborted == 0u);
    const uint32_t chunks_per_frame =
        T384_RAW16_FRAME_BYTES / T384_PIPELINE_CHUNK_BYTES;
    assert(stats.chunks_committed == chunks_per_frame);
    assert(stats.bytes_committed == T384_RAW16_FRAME_BYTES);
    assert(stats.chunks_released == chunks_per_frame);
    assert(stats.queued_chunks == 0u);
    assert(stats.high_water_chunks == 1u);

    t384_frame_chunk_view_t picture = {0};
    uint8_t picture_header[T384_RAW16_WIRE_HEADER_BYTES];
    picture.flags = T384_CHUNK_FLAG_FRAME_START |
                    T384_CHUNK_FLAG_PICTURE_UYVY;
    t384_raw16_wire_encode(picture_header, &picture);
    assert(get_le16(picture_header + 32u) == T384_FRAME_PIXEL_FORMAT_UYVY);

    t384_frame_pipeline_init();
    assert(t384_frame_pipeline_begin_frame(1u, 0u,
                                           T384_CHUNK_FLAG_TPD_Y16));
    const bool ring_holds_complete_frame =
        (uint64_t)T384_PIPELINE_SLOT_COUNT * T384_PIPELINE_CHUNK_BYTES ==
        T384_RAW16_FRAME_BYTES;
    for (unsigned i = 0u; i < T384_PIPELINE_SLOT_COUNT; ++i) {
        const bool end = ring_holds_complete_frame &&
                         i + 1u == T384_PIPELINE_SLOT_COUNT;
        produce_chunk(1u, i * T384_PIPELINE_CHUNK_BYTES, end);
    }
    if (ring_holds_complete_frame) {
        assert(t384_frame_pipeline_begin_frame(2u, 0u,
                                               T384_CHUNK_FLAG_TPD_Y16));
    }
    uint8_t *data = NULL;
    uint16_t capacity = 0u;
    assert(!t384_frame_pipeline_acquire_write(&data, &capacity));
    t384_frame_pipeline_abort_frame();
    t384_frame_pipeline_get_stats(&stats);
    assert(stats.acquire_no_slot == 1u);
    assert(stats.frames_aborted == 1u);
    assert(stats.bytes_committed ==
           (uint64_t)T384_PIPELINE_SLOT_COUNT * T384_PIPELINE_CHUNK_BYTES);
    assert(stats.high_water_chunks == T384_PIPELINE_SLOT_COUNT);

    puts("T384 RAW16 source pipeline and chunk envelope smoke passed");
    return 0;
}
