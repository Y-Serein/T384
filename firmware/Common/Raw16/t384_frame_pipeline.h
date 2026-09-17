#ifndef T384_FRAME_PIPELINE_H
#define T384_FRAME_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "t384_raw16.h"

#ifndef T384_DUALCORE
#define T384_DUALCORE 0
#endif

#define T384_PIPELINE_CHUNK_ROWS 8u
#define T384_PIPELINE_CHUNK_BYTES \
    (T384_RAW16_WIDTH * T384_RAW16_BYTES_PER_PIXEL * T384_PIPELINE_CHUNK_ROWS)
#if T384_DUALCORE
#define T384_PIPELINE_SLOT_COUNT \
    (2u * T384_RAW16_FRAME_BYTES / T384_PIPELINE_CHUNK_BYTES)
#elif T384_RAW16_FRAME_BYTES == 98304u
#define T384_PIPELINE_SLOT_COUNT 24u
#else
/* Two of the previous twelve blocks fund the real DVP ping-pong staging.
 * Combined payload RAM stays at twelve blocks; no new frame-sized buffer. */
#define T384_PIPELINE_SLOT_COUNT 10u
#endif

#define T384_CHUNK_FLAG_FRAME_START 0x0001u
#define T384_CHUNK_FLAG_FRAME_END 0x0002u
#define T384_CHUNK_FLAG_SYNTHETIC 0x0004u
#define T384_CHUNK_FLAG_TPD_Y16 0x0010u
#define T384_CHUNK_FLAG_PICTURE_UYVY 0x0020u
#define T384_CHUNK_FLAG_DATA_MODE_MASK \
    (T384_CHUNK_FLAG_TPD_Y16 | T384_CHUNK_FLAG_PICTURE_UYVY)

typedef struct {
    const uint8_t *data;
    uint32_t frame_sequence;
    uint32_t frame_offset;
    uint32_t capture_ms;
    uint16_t length;
    uint16_t flags;
} t384_frame_chunk_view_t;

typedef struct {
    uint32_t frames_started;
    uint32_t frames_completed;
    uint32_t frames_aborted;
    uint32_t chunks_committed;
    uint64_t bytes_committed;
    uint32_t chunks_released;
    uint32_t acquire_no_slot;
    uint32_t protocol_errors;
    uint32_t queued_chunks;
    uint32_t high_water_chunks;
    uint32_t producer_active;
    uint32_t producer_leased;
    uint32_t consumer_leased;
} t384_frame_pipeline_stats_t;

void t384_frame_pipeline_init(void);
bool t384_frame_pipeline_empty(void);
/*
 * Single-producer/single-consumer contract:
 * - all producer calls must be serialized by the source adapter;
 * - do not call producer metadata functions concurrently from an ISR and task;
 * - an ISR may mark source-local DMA completion, then source_task commits it;
 * - commit only after DMA/CPU writes complete; abort only after DMA stopped.
 */
bool t384_frame_pipeline_begin_frame(uint32_t frame_sequence,
                                     uint32_t capture_ms,
                                     uint16_t source_flags);
bool t384_frame_pipeline_acquire_write(uint8_t **data, uint16_t *capacity);
bool t384_frame_pipeline_commit_write(uint16_t length, bool end_of_frame);
void t384_frame_pipeline_abort_frame(void);
bool t384_frame_pipeline_peek(t384_frame_chunk_view_t *view);
void t384_frame_pipeline_release(void);
void t384_frame_pipeline_get_stats(t384_frame_pipeline_stats_t *out);

/* Main-task only, after source DMA/ISR stopped and every consumer released.
 * Exclusively borrows existing payload memory; no extra full-table buffer. */
bool t384_frame_pipeline_scratch_acquire(uint8_t **data, size_t *capacity);
void t384_frame_pipeline_scratch_release(void);

#endif
