#include "t384_dualcore.h"

#if T384_DUALCORE
#include <string.h>
#include "t384_compiler.h"
#define S t384_dualcore_shared

#if T384_PIPELINE_STREAMING
typedef struct {
    uint32_t frame_sequence, frame_offset, capture_ms;
    uint16_t length, flags;
} ring_slot_t;
typedef char ring_metadata_size_check[sizeof(ring_slot_t) == 16u ? 1 : -1];
#define R S.ring

static uint32_t next_slot(uint32_t slot)
{
    return slot + 1u == T384_PIPELINE_SLOT_COUNT ? 0u : slot + 1u;
}
static uint32_t queued_chunks(void)
{
    const uint32_t committed = __atomic_load_n(&R.committed, __ATOMIC_ACQUIRE);
    const uint32_t released = __atomic_load_n(&R.released, __ATOMIC_ACQUIRE);
    const uint32_t count = committed - released;
    return count > T384_PIPELINE_SLOT_COUNT ? T384_PIPELINE_SLOT_COUNT : count;
}
static void stats_begin(void) { ++S.producer_sequence; T384_MEMORY_BARRIER(); }
static void stats_end(void) { T384_MEMORY_BARRIER(); ++S.producer_sequence; }
void t384_frame_pipeline_init(void) { t384_dualcore_init(); }
bool t384_frame_pipeline_empty(void)
{
    return queued_chunks() == 0u && !R.active && !R.write_leased &&
           !__atomic_load_n(&S.read_leased, __ATOMIC_ACQUIRE) && !R.scratch_leased;
}
void t384_frame_pipeline_abort_frame(void)
{
    if (!R.active) return;
    stats_begin();
    ++S.pipeline.frames_aborted;
    R.active = R.write_leased = R.frame_offset = 0u;
    stats_end();
    /* Already published slots remain immutable until consumer release.
     * No FRAME_END is emitted: browser discards this prefix at next START. */
}
static bool protocol_error(void)
{
    stats_begin(); ++S.pipeline.protocol_errors; stats_end();
    t384_frame_pipeline_abort_frame();
    return false;
}
bool t384_frame_pipeline_begin_frame(uint32_t sequence, uint32_t ms, uint16_t flags)
{
    const uint16_t mode = flags & T384_CHUNK_FLAG_DATA_MODE_MASK;
    if (R.active || R.write_leased ||
        (mode != T384_CHUNK_FLAG_TPD_Y16 && mode != T384_CHUNK_FLAG_PICTURE_UYVY))
        return protocol_error();
#if T384_PIPELINE_PACKED_PICTURE
    if (mode != T384_CHUNK_FLAG_PICTURE_UYVY) return protocol_error();
#endif
    if (R.scratch_leased) return false;
    /* Admit at an empty boundary. Starting every frame behind an old prefix
     * can repeatedly overflow mid-frame and starve complete-frame delivery. */
    if (queued_chunks() || __atomic_load_n(&S.read_leased, __ATOMIC_ACQUIRE)) {
        stats_begin(); ++S.pipeline.acquire_no_slot; stats_end();
        return false;
    }
    stats_begin();
    R.active = 1u;
    R.frame_sequence = sequence; R.capture_ms = ms; R.source_flags = flags;
    R.frame_offset = 0u;
    ++S.pipeline.frames_started;
    stats_end();
    return true;
}
bool t384_frame_pipeline_acquire_write(uint8_t **data, uint16_t *capacity)
{
    if (!data || !capacity || !R.active || R.write_leased)
        return protocol_error();
    if (queued_chunks() == T384_PIPELINE_SLOT_COUNT) {
        stats_begin(); ++S.pipeline.acquire_no_slot; stats_end();
        return false;
    }
    stats_begin();
    R.write_slot = R.producer_next; R.write_leased = 1u;
    stats_end();
#if T384_PIPELINE_PACKED_PICTURE
    *data = t384_picture_slot_data(R.write_slot);
#else
    *data = t384_dualcore_frame + R.write_slot * T384_PIPELINE_CHUNK_BYTES;
#endif
    *capacity = T384_PIPELINE_STORAGE_CHUNK_BYTES;
    return true;
}
bool t384_frame_pipeline_commit_write(uint16_t length, bool end)
{
    if (!R.active || !R.write_leased || length != T384_PIPELINE_CHUNK_BYTES ||
        R.frame_offset + length > T384_RAW16_FRAME_BYTES ||
        end != (R.frame_offset + length == T384_RAW16_FRAME_BYTES))
        return protocol_error();
    ring_slot_t slot = {R.frame_sequence, R.frame_offset, R.capture_ms,
                       length, (uint16_t)R.source_flags};

    if (R.frame_offset == 0u) slot.flags |= T384_CHUNK_FLAG_FRAME_START;
    if (end) slot.flags |= T384_CHUNK_FLAG_FRAME_END;
#if T384_PIPELINE_PACKED_PICTURE && T384_NETWORK_ON_V5F
    memcpy(t384_picture_slot_metadata() + R.write_slot * sizeof(slot),
           &slot, sizeof(slot));
#else
    memcpy(t384_frame1_itcm + R.write_slot * sizeof(slot), &slot, sizeof(slot));
#endif

    stats_begin();
    R.frame_offset += length; R.write_leased = 0u;
    ++S.pipeline.chunks_committed; S.pipeline.bytes_committed += length;
    R.producer_next = next_slot(R.producer_next);
    const uint32_t committed = __atomic_load_n(&R.committed, __ATOMIC_RELAXED);
    __atomic_store_n(&R.committed, committed + 1u, __ATOMIC_RELEASE);
    const uint32_t count = queued_chunks();
    if (count > S.pipeline.high_water_chunks) S.pipeline.high_water_chunks = count;
    if (end) { R.active = 0u; R.frame_offset = 0u; ++S.pipeline.frames_completed; }
    stats_end();
    return true;
}
bool t384_frame_pipeline_peek(t384_frame_chunk_view_t *view)
{
    if (!view || R.scratch_leased) return false;
    uint32_t expected = 0u;
    if (!__atomic_compare_exchange_n(&S.read_leased, &expected, 1u, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    if (!queued_chunks() || R.scratch_leased) {
        __atomic_store_n(&S.read_leased, 0u, __ATOMIC_RELEASE);
        return false;
    }
    R.read_slot = R.consumer_next;
    ring_slot_t slot;
#if T384_PIPELINE_PACKED_PICTURE && T384_NETWORK_ON_V5F
    memcpy(&slot, t384_picture_slot_metadata() + R.read_slot * sizeof(slot),
           sizeof(slot));
#else
    memcpy(&slot, t384_frame1_itcm + R.read_slot * sizeof(slot), sizeof(slot));
#endif
#if T384_PIPELINE_PACKED_PICTURE
    if (slot.length != T384_PIPELINE_CHUNK_BYTES ||
        (slot.flags & T384_CHUNK_FLAG_DATA_MODE_MASK) != T384_CHUNK_FLAG_PICTURE_UYVY) {
        __atomic_store_n(&S.read_leased, 0u, __ATOMIC_RELEASE);
        return false;
    }
    view->data = t384_picture_slot_data(R.read_slot);
#else
    view->data = t384_dualcore_frame + R.read_slot * T384_PIPELINE_CHUNK_BYTES;
#endif
    view->frame_sequence = slot.frame_sequence; view->frame_offset = slot.frame_offset;
    view->capture_ms = slot.capture_ms; view->length = slot.length; view->flags = slot.flags;
    return true;
}
void t384_frame_pipeline_release(void)
{
    if (!__atomic_load_n(&S.read_leased, __ATOMIC_ACQUIRE)) return;
    R.consumer_next = next_slot(R.consumer_next);
    ++S.pipeline.chunks_released;
    const uint32_t released = __atomic_load_n(&R.released, __ATOMIC_RELAXED);
    __atomic_store_n(&R.released, released + 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&S.read_leased, 0u, __ATOMIC_RELEASE);
}
bool t384_frame_pipeline_scratch_acquire(uint8_t **data, size_t *capacity)
{
    if (!data || !capacity || !t384_frame_pipeline_empty()) return false;
    uint32_t expected = 0u;
    if (!__atomic_compare_exchange_n(&S.read_leased, &expected, 1u, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    R.scratch_leased = 1u;
#if T384_NETWORK_ON_V5F
    *data = (uint8_t *)(uintptr_t)0x200C0300u;
#else
    *data = t384_dualcore_frame;
#endif
    *capacity = T384_CAPTURE_BUFFER_BYTES;
    __atomic_store_n(&S.read_leased, 0u, __ATOMIC_RELEASE);
    return true;
}
void t384_frame_pipeline_scratch_release(void) { R.scratch_leased = 0u; }
void t384_frame_pipeline_get_stats(t384_frame_pipeline_stats_t *out)
{
    static t384_frame_pipeline_stats_t last;
    if (!out) return;
    for (unsigned attempt = 0u; attempt < 16u; ++attempt) {
        const uint32_t before = __atomic_load_n(&S.producer_sequence, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        t384_frame_pipeline_stats_t copy = S.pipeline;
        T384_MEMORY_BARRIER();
        if (before == __atomic_load_n(&S.producer_sequence, __ATOMIC_ACQUIRE)) { last = copy; break; }
    }
    *out = last;
    out->queued_chunks = queued_chunks(); out->producer_active = R.active;
    out->producer_leased = R.write_leased; out->consumer_leased = S.read_leased;
}
#else

static void stats_begin(void) { ++S.producer_sequence; T384_MEMORY_BARRIER(); }
static void stats_end(void) { T384_MEMORY_BARRIER(); ++S.producer_sequence; }

static uint32_t bank_state(const t384_frame_bank_t *bank)
{
    return __atomic_load_n(&bank->state, __ATOMIC_ACQUIRE);
}

static uint32_t queued_chunks(void)
{
    uint32_t queued = 0u;
    for (unsigned i = 0u; i < T384_FRAME_BANKS; ++i) {
        const t384_frame_bank_t *bank = &S.banks[i];
        const uint32_t state = bank_state(bank);
        if (state == T384_FRAME_READY || state == T384_FRAME_READING)
            queued += (T384_RAW16_FRAME_BYTES - bank->read_offset) /
                       T384_PIPELINE_CHUNK_BYTES;
    }
    return queued;
}

void t384_frame_pipeline_init(void) { t384_dualcore_init(); }

bool t384_frame_pipeline_empty(void)
{
    for (unsigned i = 0u; i < T384_FRAME_BANKS; ++i)
        if (bank_state(&S.banks[i]) != T384_FRAME_FREE) return false;
    return true;
}

void t384_frame_pipeline_abort_frame(void)
{
    t384_frame_bank_t *bank = &S.banks[S.producer_bank];
    if (bank_state(bank) != T384_FRAME_FILLING) return;
    stats_begin();
    ++S.pipeline.frames_aborted;
    bank->write_leased = bank->write_offset = 0u;
    stats_end();
    __atomic_store_n(&bank->state, T384_FRAME_FREE, __ATOMIC_RELEASE);
}

static bool protocol_error(void)
{
    stats_begin(); ++S.pipeline.protocol_errors; stats_end();
    t384_frame_pipeline_abort_frame();
    return false;
}

bool t384_frame_pipeline_begin_frame(uint32_t sequence, uint32_t ms,
                                     uint16_t flags)
{
    const uint16_t mode = flags & T384_CHUNK_FLAG_DATA_MODE_MASK;
    if ((mode != T384_CHUNK_FLAG_TPD_Y16 &&
         mode != T384_CHUNK_FLAG_PICTURE_UYVY) ||
        bank_state(&S.banks[S.producer_bank]) == T384_FRAME_FILLING)
        return protocol_error();
    for (unsigned i = 0u; i < T384_FRAME_BANKS; ++i) {
        t384_frame_bank_t *bank = &S.banks[i];
        uint32_t expected = T384_FRAME_FREE;
        if (!__atomic_compare_exchange_n(&bank->state, &expected,
                 T384_FRAME_FILLING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;
        stats_begin();
        S.producer_bank = i;
        bank->frame_sequence = sequence;
        bank->capture_ms = ms;
        bank->source_flags = flags;
        bank->write_offset = bank->read_offset = bank->write_leased = 0u;
        ++S.pipeline.frames_started;
        stats_end();
        return true;
    }
    stats_begin(); ++S.pipeline.acquire_no_slot; stats_end();
    return false;
}

bool t384_frame_pipeline_acquire_write(uint8_t **data, uint16_t *capacity)
{
    t384_frame_bank_t *bank = &S.banks[S.producer_bank];
    if (!data || !capacity || bank_state(bank) != T384_FRAME_FILLING ||
        bank->write_leased || bank->write_offset >= T384_RAW16_FRAME_BYTES)
        return protocol_error();
    stats_begin(); bank->write_leased = 1u; stats_end();
    *data = t384_frame_bank_data(S.producer_bank, bank->write_offset);
    *capacity = T384_PIPELINE_CHUNK_BYTES;
    return true;
}

bool t384_frame_pipeline_commit_write(uint16_t length, bool end)
{
    t384_frame_bank_t *bank = &S.banks[S.producer_bank];
    if (bank_state(bank) != T384_FRAME_FILLING || !bank->write_leased ||
        length != T384_PIPELINE_CHUNK_BYTES ||
        bank->write_offset + length > T384_RAW16_FRAME_BYTES ||
        end != (bank->write_offset + length == T384_RAW16_FRAME_BYTES))
        return protocol_error();
    stats_begin();
    bank->write_offset += length;
    bank->write_leased = 0u;
    ++S.pipeline.chunks_committed;
    S.pipeline.bytes_committed += length;
    if (end) {
        ++S.pipeline.frames_completed;
        const uint32_t high = queued_chunks() +
            T384_RAW16_FRAME_BYTES / T384_PIPELINE_CHUNK_BYTES;
        if (high > S.pipeline.high_water_chunks) S.pipeline.high_water_chunks = high;
    }
    stats_end();
    /* Publish only after the source validates every row and physical frame end. */
    if (end) __atomic_store_n(&bank->state, T384_FRAME_READY, __ATOMIC_RELEASE);
    return true;
}

bool t384_frame_pipeline_peek(t384_frame_chunk_view_t *view)
{
    if (!view) return false;
    uint32_t expected = 0u;
    if (!__atomic_compare_exchange_n(&S.read_leased, &expected, 1u, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    unsigned selected = S.consumer_bank;
    if (bank_state(&S.banks[selected]) != T384_FRAME_READING) {
        selected = T384_FRAME_BANKS;
        for (unsigned i = 0u; i < T384_FRAME_BANKS; ++i) {
            if (bank_state(&S.banks[i]) != T384_FRAME_READY) continue;
            if (selected == T384_FRAME_BANKS ||
                (int32_t)(S.banks[i].frame_sequence -
                          S.banks[selected].frame_sequence) < 0) selected = i;
        }
        if (selected == T384_FRAME_BANKS) {
            __atomic_store_n(&S.read_leased, 0u, __ATOMIC_RELEASE);
            return false;
        }
        S.consumer_bank = selected;
        __atomic_store_n(&S.banks[selected].state, T384_FRAME_READING, __ATOMIC_RELEASE);
    }
    const t384_frame_bank_t *bank = &S.banks[selected];
    view->data = t384_frame_bank_data(selected, bank->read_offset);
    view->frame_sequence = bank->frame_sequence;
    view->frame_offset = bank->read_offset;
    view->capture_ms = bank->capture_ms;
    view->length = T384_PIPELINE_CHUNK_BYTES;
    view->flags = (uint16_t)bank->source_flags;
    if (bank->read_offset == 0u) view->flags |= T384_CHUNK_FLAG_FRAME_START;
    if (bank->read_offset + view->length == T384_RAW16_FRAME_BYTES)
        view->flags |= T384_CHUNK_FLAG_FRAME_END;
    return true;
}

void t384_frame_pipeline_release(void)
{
    if (!__atomic_load_n(&S.read_leased, __ATOMIC_ACQUIRE)) return;
    t384_frame_bank_t *bank = &S.banks[S.consumer_bank];
    bank->read_offset += T384_PIPELINE_CHUNK_BYTES;
    ++S.pipeline.chunks_released;
    if (bank->read_offset == T384_RAW16_FRAME_BYTES)
        __atomic_store_n(&bank->state, T384_FRAME_FREE, __ATOMIC_RELEASE);
    __atomic_store_n(&S.read_leased, 0u, __ATOMIC_RELEASE);
}

bool t384_frame_pipeline_scratch_acquire(uint8_t **data, size_t *capacity)
{
    if (!data || !capacity) return false;
    for (unsigned i = 0u; i < T384_FRAME_BANKS; ++i) {
        uint32_t expected = T384_FRAME_FREE;
        if (!__atomic_compare_exchange_n(&S.banks[i].state, &expected,
                 T384_FRAME_SCRATCH, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            while (i > 0u) {
                --i;
                __atomic_store_n(&S.banks[i].state, T384_FRAME_FREE, __ATOMIC_RELEASE);
            }
            return false;
        }
    }
    /* File transfers retain the original contiguous bank and reserve both. */
    *data = t384_dualcore_frame;
    *capacity = T384_RAW16_FRAME_BYTES;
    return true;
}

void t384_frame_pipeline_scratch_release(void)
{
    for (unsigned i = 0u; i < T384_FRAME_BANKS; ++i)
        if (bank_state(&S.banks[i]) == T384_FRAME_SCRATCH)
            __atomic_store_n(&S.banks[i].state, T384_FRAME_FREE, __ATOMIC_RELEASE);
}

void t384_frame_pipeline_get_stats(t384_frame_pipeline_stats_t *out)
{
    static t384_frame_pipeline_stats_t last;
    if (!out) return;
    for (unsigned attempt = 0u; attempt < 16u; ++attempt) {
        const uint32_t before = __atomic_load_n(&S.producer_sequence, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        t384_frame_pipeline_stats_t copy = S.pipeline;
        T384_MEMORY_BARRIER();
        if (before == __atomic_load_n(&S.producer_sequence, __ATOMIC_ACQUIRE)) {
            last = copy; break;
        }
    }
    *out = last;
    const t384_frame_bank_t *producer = &S.banks[S.producer_bank];
    out->producer_active = bank_state(producer) == T384_FRAME_FILLING;
    out->producer_leased = producer->write_leased;
    out->consumer_leased = S.read_leased;
    out->queued_chunks = queued_chunks();
}
#endif /* streaming / complete-frame banks */
#endif
