#include "t384_frame_pipeline.h"

#include <stddef.h>
#include <string.h>

#include "t384_compiler.h"

#if T384_PIPELINE_CHUNK_BYTES != 4096u && T384_PIPELINE_CHUNK_BYTES != 6144u
#error "unsupported T384 pipeline chunk size"
#endif

typedef struct {
    uint32_t frame_sequence;
    uint32_t frame_offset;
    uint32_t capture_ms;
    uint16_t length;
    uint16_t flags;
} pipeline_slot_t;
typedef char pipeline_slot_metadata_must_be_16_bytes[
    sizeof(pipeline_slot_t) == 16u ? 1 : -1];

static uint8_t slot_data[T384_PIPELINE_SLOT_COUNT][T384_PIPELINE_CHUNK_BYTES]
    __attribute__((aligned(32)));
static pipeline_slot_t slots[T384_PIPELINE_SLOT_COUNT];
static uint32_t committed_count;
static uint32_t released_count;
static volatile uint32_t producer_stats_sequence;
static volatile bool producer_active;
static volatile bool producer_leased;
static bool consumer_leased;
static uint32_t producer_frame_sequence;
static uint32_t producer_frame_offset;
static uint32_t producer_capture_ms;
static uint16_t producer_source_flags;
static uint32_t producer_slot_index;
static uint32_t consumer_slot_index;
static uint32_t producer_next_slot;
static uint32_t consumer_next_slot;
static volatile t384_frame_pipeline_stats_t stats;

static uint32_t next_slot(uint32_t current)
{
    ++current;
    return current == T384_PIPELINE_SLOT_COUNT ? 0u : current;
}

static uint32_t queued_count(void)
{
    const uint32_t committed =
        __atomic_load_n(&committed_count, __ATOMIC_ACQUIRE);
    const uint32_t released =
        __atomic_load_n(&released_count, __ATOMIC_ACQUIRE);
    uint32_t queued = committed - released;
    if (queued > T384_PIPELINE_SLOT_COUNT) {
        queued = T384_PIPELINE_SLOT_COUNT;
    }
    return queued;
}

static void producer_stats_begin(void)
{
    ++producer_stats_sequence;
    T384_MEMORY_BARRIER();
}

static void producer_stats_end(void)
{
    T384_MEMORY_BARRIER();
    ++producer_stats_sequence;
}

static void protocol_error(void)
{
    producer_stats_begin();
    ++stats.protocol_errors;
    producer_active = false;
    producer_leased = false;
    producer_frame_offset = 0u;
    producer_stats_end();
}

void t384_frame_pipeline_init(void)
{
    memset(slot_data, 0, sizeof(slot_data));
    memset(slots, 0, sizeof(slots));
    memset((void *)&stats, 0, sizeof(stats));
    committed_count = 0u;
    released_count = 0u;
    producer_stats_sequence = 0u;
    producer_active = false;
    producer_leased = false;
    consumer_leased = false;
    producer_frame_offset = 0u;
    producer_next_slot = 0u;
    consumer_next_slot = 0u;
}

bool t384_frame_pipeline_empty(void)
{
    return queued_count() == 0u && !producer_active && !producer_leased;
}

bool t384_frame_pipeline_begin_frame(uint32_t frame_sequence,
                                     uint32_t capture_ms,
                                     uint16_t source_flags)
{
    if (producer_active || producer_leased) {
        protocol_error();
        return false;
    }
    producer_stats_begin();
    producer_active = true;
    producer_frame_sequence = frame_sequence;
    producer_frame_offset = 0u;
    producer_capture_ms = capture_ms;
    producer_source_flags = source_flags;
    ++stats.frames_started;
    producer_stats_end();
    return true;
}

bool t384_frame_pipeline_acquire_write(uint8_t **data, uint16_t *capacity)
{
    if (data == NULL || capacity == NULL || !producer_active ||
        producer_leased) {
        protocol_error();
        return false;
    }
    const uint32_t committed =
        __atomic_load_n(&committed_count, __ATOMIC_RELAXED);
    const uint32_t released =
        __atomic_load_n(&released_count, __ATOMIC_ACQUIRE);
    if (committed - released >= T384_PIPELINE_SLOT_COUNT) {
        producer_stats_begin();
        ++stats.acquire_no_slot;
        producer_stats_end();
        return false;
    }

    producer_slot_index = producer_next_slot;
    producer_stats_begin();
    producer_leased = true;
    producer_stats_end();
    *data = slot_data[producer_slot_index];
    *capacity = T384_PIPELINE_CHUNK_BYTES;
    return true;
}

bool t384_frame_pipeline_commit_write(uint16_t length, bool end_of_frame)
{
    if (!producer_active || !producer_leased || length == 0u ||
        length > T384_PIPELINE_CHUNK_BYTES ||
        producer_frame_offset + length > T384_RAW16_FRAME_BYTES) {
        protocol_error();
        return false;
    }

    const bool reaches_frame_end =
        producer_frame_offset + length == T384_RAW16_FRAME_BYTES;
    if (reaches_frame_end != end_of_frame) {
        protocol_error();
        return false;
    }

    pipeline_slot_t *slot = &slots[producer_slot_index];
    slot->frame_sequence = producer_frame_sequence;
    slot->frame_offset = producer_frame_offset;
    slot->capture_ms = producer_capture_ms;
    slot->length = length;
    slot->flags = producer_source_flags;
    if (producer_frame_offset == 0u) {
        slot->flags |= T384_CHUNK_FLAG_FRAME_START;
    }
    if (end_of_frame) {
        slot->flags |= T384_CHUNK_FLAG_FRAME_END;
    }

    producer_stats_begin();
    producer_frame_offset += length;
    producer_leased = false;
    ++stats.chunks_committed;
    stats.bytes_committed += length;
    const uint32_t committed =
        __atomic_load_n(&committed_count, __ATOMIC_RELAXED);
    T384_MEMORY_BARRIER();
    __atomic_store_n(&committed_count, committed + 1u, __ATOMIC_RELEASE);
    producer_next_slot = next_slot(producer_next_slot);

    const uint32_t queued = queued_count();
    if (queued > stats.high_water_chunks) {
        stats.high_water_chunks = queued;
    }
    if (end_of_frame) {
        producer_active = false;
        producer_frame_offset = 0u;
        ++stats.frames_completed;
    }
    producer_stats_end();
    return true;
}

void t384_frame_pipeline_abort_frame(void)
{
    if (producer_active || producer_leased) {
        producer_stats_begin();
        ++stats.frames_aborted;
        producer_active = false;
        producer_leased = false;
        producer_frame_offset = 0u;
        producer_stats_end();
        return;
    }
}

bool t384_frame_pipeline_peek(t384_frame_chunk_view_t *view)
{
    if (view == NULL || consumer_leased) {
        return false;
    }
    const uint32_t released =
        __atomic_load_n(&released_count, __ATOMIC_RELAXED);
    const uint32_t committed =
        __atomic_load_n(&committed_count, __ATOMIC_ACQUIRE);
    if (released == committed) {
        return false;
    }

    T384_MEMORY_BARRIER();
    consumer_slot_index = consumer_next_slot;
    const pipeline_slot_t *slot = &slots[consumer_slot_index];
    view->data = slot_data[consumer_slot_index];
    view->frame_sequence = slot->frame_sequence;
    view->frame_offset = slot->frame_offset;
    view->capture_ms = slot->capture_ms;
    view->length = slot->length;
    view->flags = slot->flags;
    consumer_leased = true;
    return true;
}

void t384_frame_pipeline_release(void)
{
    if (!consumer_leased) {
        return;
    }
    const uint32_t released =
        __atomic_load_n(&released_count, __ATOMIC_RELAXED);
    T384_MEMORY_BARRIER();
    __atomic_store_n(&released_count, released + 1u, __ATOMIC_RELEASE);
    consumer_next_slot = next_slot(consumer_next_slot);
    consumer_leased = false;
    ++stats.chunks_released;
}

void t384_frame_pipeline_get_stats(t384_frame_pipeline_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    uint32_t sequence_before;
    uint32_t sequence_after;
    for (;;) {
        sequence_before = producer_stats_sequence;
        if ((sequence_before & 1u) != 0u) {
            continue;
        }
        T384_MEMORY_BARRIER();
        out->frames_started = stats.frames_started;
        out->frames_completed = stats.frames_completed;
        out->frames_aborted = stats.frames_aborted;
        out->chunks_committed = stats.chunks_committed;
        out->bytes_committed = stats.bytes_committed;
        out->acquire_no_slot = stats.acquire_no_slot;
        out->protocol_errors = stats.protocol_errors;
        out->high_water_chunks = stats.high_water_chunks;
        out->producer_active = producer_active ? 1u : 0u;
        out->producer_leased = producer_leased ? 1u : 0u;
        T384_MEMORY_BARRIER();
        sequence_after = producer_stats_sequence;
        if (sequence_before == sequence_after &&
            (sequence_after & 1u) == 0u) {
            break;
        }
    }
    out->chunks_released = stats.chunks_released;
    out->queued_chunks = queued_count();
    out->consumer_leased = consumer_leased ? 1u : 0u;
}
