#include "t384_dualcore.h"

#if T384_DUALCORE
#include <string.h>
#include "t384_compiler.h"
#define S t384_dualcore_shared

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
#endif
