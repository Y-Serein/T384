/* Exercise the production 640 Y16 four-region streaming ring.  This is a
 * host fixture: it supplies only linker-owned storage, never device I/O. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "t384_dualcore.h"
#include "t384_raw16_wire.h"

#if !T384_PIPELINE_640_Y16 || T384_PIPELINE_PACKED_PICTURE
#error "Requires the enabled 640 Y16 streaming profile"
#endif

t384_dualcore_shared_t t384_dualcore_shared;
uint8_t t384_dualcore_frame[T384_CAPTURE_BUFFER_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame_shared_payload[T384_FRAME_SHARED_PAYLOAD_BYTES]
    __attribute__((aligned(32)));
uint8_t t384_frame_shared_metadata[T384_FRAME_SHARED_METADATA_BYTES]
    __attribute__((aligned(32)));
uint8_t t384_frame_extra_code[T384_FRAME_EXTRA_CODE_BYTES]
    __attribute__((aligned(32)));
uint8_t t384_frame_extra_data[T384_FRAME_EXTRA_DATA_BYTES]
    __attribute__((aligned(32)));
uint8_t t384_frame1_itcm[T384_FRAME1_ITCM_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_dtcm[T384_FRAME1_DTCM_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_code[T384_FRAME1_CODE_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_data[T384_FRAME1_DATA_BYTES] __attribute__((aligned(32)));

void t384_dualcore_init(void)
{
    memset(&t384_dualcore_shared, 0, sizeof(t384_dualcore_shared));
}

static uint8_t *expected_slot(uint32_t slot)
{
    if (slot < 43u)
        return t384_dualcore_frame + slot * T384_PIPELINE_CHUNK_BYTES;
    slot -= 43u;
    if (slot < 32u)
        return t384_frame_shared_payload + slot * T384_PIPELINE_CHUNK_BYTES;
    slot -= 32u;
    if (slot < 26u)
        return t384_frame_extra_code + slot * T384_PIPELINE_CHUNK_BYTES;
    slot -= 26u;
    if (slot < 9u)
        return t384_frame_extra_data + slot * T384_PIPELINE_CHUNK_BYTES;
    return NULL;
}

static void assert_slot_map(void)
{
    assert(T384_PIPELINE_SLOT_COUNT == 110u);
    assert(T384_PIPELINE_CHUNK_BYTES == 5120u);
    assert(T384_CAPTURE_BUFFER_BYTES == 43u * 5120u);
    assert(T384_FRAME_SHARED_PAYLOAD_BYTES == 32u * 5120u);
    assert(T384_FRAME_EXTRA_CODE_BYTES == 26u * 5120u);
    assert(T384_FRAME_EXTRA_DATA_BYTES == 9u * 5120u);
    assert(T384_FRAME_SHARED_METADATA_BYTES == 2080u);
    for (uint32_t slot = 0u; slot < T384_PIPELINE_SLOT_COUNT; ++slot)
        assert(t384_frame_slot_data(slot) == expected_slot(slot));
    assert(t384_frame_slot_data(T384_PIPELINE_SLOT_COUNT) == NULL);
    assert(t384_frame_slot_metadata() == t384_frame_shared_metadata + 32u);
}

static void assert_y16_chunk(const t384_frame_chunk_view_t *view,
                             uint32_t sequence, uint32_t block)
{
    assert(view->frame_sequence == sequence);
    assert(view->frame_offset == block * T384_PIPELINE_CHUNK_BYTES);
    assert(view->length == T384_PIPELINE_CHUNK_BYTES);
    assert((view->flags & T384_CHUNK_FLAG_DATA_MODE_MASK) ==
           T384_CHUNK_FLAG_TPD_Y16);
    assert(((view->flags & T384_CHUNK_FLAG_FRAME_START) != 0u) ==
           (block == 0u));
    assert(((view->flags & T384_CHUNK_FLAG_FRAME_END) != 0u) ==
           (block + 1u == T384_RAW16_FRAME_BYTES / T384_PIPELINE_CHUNK_BYTES));
    const uint16_t first = t384_raw16_word(sequence,
        view->frame_offset / T384_RAW16_BYTES_PER_PIXEL);
    assert(view->data[0] == (uint8_t)(first >> 8));
    assert(view->data[1] == (uint8_t)first);
    uint8_t envelope[T384_RAW16_WIRE_HEADER_BYTES];
    t384_raw16_wire_encode(envelope, view);
    assert(envelope[0] == 'T' && envelope[1] == '3');
}

/* A producer may reuse a slot only after its chunk has been released. This
 * represents normal TCP draining; it does not claim 110 slots hold 128. */
static void write_and_drain_frame(uint32_t sequence)
{
    const uint32_t blocks = T384_RAW16_FRAME_BYTES / T384_PIPELINE_CHUNK_BYTES;
    assert(blocks == 128u);
    const uint32_t first_slot = t384_dualcore_shared.ring.producer_next;
    assert(t384_frame_pipeline_begin_frame(sequence, 77u,
           T384_CHUNK_FLAG_TPD_Y16));
    for (uint32_t block = 0u; block < blocks; ++block) {
        uint8_t *data;
        uint16_t capacity;
        assert(t384_frame_pipeline_acquire_write(&data, &capacity));
        assert(capacity == T384_PIPELINE_CHUNK_BYTES);
        assert(data == expected_slot((first_slot + block) %
                                     T384_PIPELINE_SLOT_COUNT));
        assert(t384_raw16_fill(sequence, block * T384_PIPELINE_CHUNK_BYTES,
                               data, capacity) == capacity);
        assert(t384_frame_pipeline_commit_write(capacity, block + 1u == blocks));
        t384_frame_chunk_view_t view;
        assert(t384_frame_pipeline_peek(&view));
        assert_y16_chunk(&view, sequence, block);
        t384_frame_pipeline_release();
    }
    assert(t384_frame_pipeline_empty());
}

static void fill_until_no_slot(uint32_t sequence)
{
    assert(t384_frame_pipeline_begin_frame(sequence, 0u,
           T384_CHUNK_FLAG_TPD_Y16));
    for (uint32_t block = 0u; block < T384_PIPELINE_SLOT_COUNT; ++block) {
        uint8_t *data;
        uint16_t capacity;
        assert(t384_frame_pipeline_acquire_write(&data, &capacity));
        assert(t384_raw16_fill(sequence, block * T384_PIPELINE_CHUNK_BYTES,
                               data, capacity) == capacity);
        assert(t384_frame_pipeline_commit_write(capacity, false));
    }
    uint8_t *data;
    uint16_t capacity;
    assert(!t384_frame_pipeline_acquire_write(&data, &capacity));
    t384_frame_pipeline_abort_frame();
}

static void drain_without_end(uint32_t sequence)
{
    for (uint32_t block = 0u; block < T384_PIPELINE_SLOT_COUNT; ++block) {
        t384_frame_chunk_view_t view;
        assert(t384_frame_pipeline_peek(&view));
        assert_y16_chunk(&view, sequence, block);
        assert((view.flags & T384_CHUNK_FLAG_FRAME_END) == 0u);
        t384_frame_pipeline_release();
    }
    assert(t384_frame_pipeline_empty());
}

int main(void)
{
    assert_slot_map();
    t384_frame_pipeline_init();
    write_and_drain_frame(7u);

    t384_frame_pipeline_stats_t stats;
    t384_frame_pipeline_get_stats(&stats);
    assert(stats.frames_completed == 1u && stats.frames_aborted == 0u);
    assert(stats.protocol_errors == 0u);

    /* The 111th acquisition fails at the finite ring boundary. Abort emits
     * no END; draining the prefix leaves the next frame admissible. */
    fill_until_no_slot(8u);
    t384_frame_pipeline_get_stats(&stats);
    assert(stats.acquire_no_slot == 1u && stats.frames_aborted == 1u);
    drain_without_end(8u);
    write_and_drain_frame(9u);

    /* A consumer lease pins the payload. Filling the remaining ring must not
     * reuse that slot; its bytes remain intact until the explicit release. */
    assert(t384_frame_pipeline_begin_frame(10u, 0u, T384_CHUNK_FLAG_TPD_Y16));
    uint8_t *data;
    uint16_t capacity;
    assert(t384_frame_pipeline_acquire_write(&data, &capacity));
    assert(t384_raw16_fill(10u, 0u, data, capacity) == capacity);
    assert(t384_frame_pipeline_commit_write(capacity, false));
    t384_frame_chunk_view_t held;
    assert(t384_frame_pipeline_peek(&held));
    uint8_t held_prefix[32];
    memcpy(held_prefix, held.data, sizeof(held_prefix));
    for (uint32_t block = 1u; block < T384_PIPELINE_SLOT_COUNT; ++block) {
        assert(t384_frame_pipeline_acquire_write(&data, &capacity));
        assert(t384_raw16_fill(10u, block * T384_PIPELINE_CHUNK_BYTES,
                               data, capacity) == capacity);
        assert(t384_frame_pipeline_commit_write(capacity, false));
    }
    assert(!t384_frame_pipeline_acquire_write(&data, &capacity));
    assert(memcmp(held_prefix, held.data, sizeof(held_prefix)) == 0);
    t384_frame_pipeline_abort_frame();
    t384_frame_pipeline_release();
    for (uint32_t block = 1u; block < T384_PIPELINE_SLOT_COUNT; ++block) {
        t384_frame_chunk_view_t view;
        assert(t384_frame_pipeline_peek(&view));
        assert_y16_chunk(&view, 10u, block);
        assert((view.flags & T384_CHUNK_FLAG_FRAME_END) == 0u);
        t384_frame_pipeline_release();
    }
    assert(t384_frame_pipeline_empty());
    write_and_drain_frame(12u);
    puts("640 Y16 four-region ring: boundaries, leases, abort and recovery passed");
    return 0;
}
