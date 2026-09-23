#ifndef T384_DUALCORE_H
#define T384_DUALCORE_H

#include "t384_frame_pipeline.h"
#include "t384_frame_source.h"
#include "t384_module_files.h"

#ifndef T384_DUALCORE
#define T384_DUALCORE 0
#endif

#define T384_V5F_ENTRY 0x00030000u
#define T384_IPC_MAGIC 0x54334231u
#define T384_IPC_VERSION 6u
#define T384_DTCM_PROBE 0x16384288u
#define T384_FRAME_BANKS 2u
#if T384_PIPELINE_STREAMING
#if T384_PIPELINE_PACKED_PICTURE
#if T384_NETWORK_ON_V5F
/* The V5F DTCM window holds the first half of the packed ring.  The second
 * half and the ring metadata live in a reserved shared-SRAM extension; this
 * keeps the complete 640 packed frame available without overlapping the
 * V5F HTTP/NCM/lwIP sections. */
#define T384_FRAME_LOCAL_SLOT_COUNT 64u
#define T384_FRAME_SHARED_SLOT_COUNT \
    (T384_PIPELINE_SLOT_COUNT - T384_FRAME_LOCAL_SLOT_COUNT)
#define T384_CAPTURE_BUFFER_BYTES \
    (T384_FRAME_LOCAL_SLOT_COUNT * T384_PIPELINE_STORAGE_CHUNK_BYTES)
#define T384_FRAME_SHARED_PAYLOAD_BYTES \
    (T384_FRAME_SHARED_SLOT_COUNT * T384_PIPELINE_STORAGE_CHUNK_BYTES)
#define T384_FRAME_SHARED_METADATA_BYTES \
    (T384_PIPELINE_SLOT_COUNT * 16u + 32u)
#define T384_FRAME1_ITCM_BYTES 32u
#define T384_FRAME1_DTCM_BYTES 32u
#define T384_FRAME1_CODE_BYTES 32u
#define T384_FRAME1_DATA_BYTES 32u
#else
/* Existing SRAM partitions only; ITCM holds word-access metadata, no pixels.
 * 85+7+16+20 slots; final shared-data space holds the HTTP read lease. */
#define T384_CAPTURE_BUFFER_BYTES (85u * T384_PIPELINE_STORAGE_CHUNK_BYTES)
#define T384_FRAME1_ITCM_BYTES (T384_PIPELINE_SLOT_COUNT * 16u)
#define T384_FRAME1_DTCM_BYTES (7u * T384_PIPELINE_STORAGE_CHUNK_BYTES)
#define T384_FRAME1_CODE_BYTES (16u * T384_PIPELINE_STORAGE_CHUNK_BYTES)
#define T384_FRAME1_DATA_BYTES (20u * T384_PIPELINE_STORAGE_CHUNK_BYTES + T384_PIPELINE_CHUNK_BYTES)
#endif
#else
#define T384_CAPTURE_BUFFER_BYTES (T384_PIPELINE_SLOT_COUNT * T384_PIPELINE_CHUNK_BYTES)
#define T384_FRAME1_ITCM_BYTES (T384_PIPELINE_SLOT_COUNT * 16u)
/* Keep the existing cross-core address probes; these are no longer frames. */
#define T384_FRAME1_DTCM_BYTES 32u
#define T384_FRAME1_CODE_BYTES 32u
#define T384_FRAME1_DATA_BYTES 32u
#endif
#else
#define T384_CAPTURE_BUFFER_BYTES T384_RAW16_FRAME_BYTES
#define T384_FRAME1_ITCM_BYTES (96u * 1024u)
#define T384_FRAME1_DTCM_BYTES (18u * 1024u)
#define T384_FRAME1_CODE_BYTES (42u * 1024u)
#define T384_FRAME1_DATA_BYTES (60u * 1024u)
#endif
#define T384_FRAME_PROBE_REGIONS 5u

enum { T384_FRAME_FREE, T384_FRAME_FILLING, T384_FRAME_READY,
       T384_FRAME_READING, T384_FRAME_SCRATCH };
enum { T384_RPC_NONE, T384_RPC_FILE_START, T384_RPC_FILE_ABORT,
       T384_RPC_FILE_DOWNLOAD, T384_RPC_FILE_RELEASE };

typedef struct {
    uint32_t state, frame_sequence, capture_ms, source_flags;
    uint32_t write_offset, read_offset, write_leased;
} t384_frame_bank_t;

typedef struct {
    uint32_t committed, released, active, write_leased, scratch_leased;
    uint32_t frame_sequence, capture_ms, source_flags, frame_offset;
    uint32_t producer_next, consumer_next, write_slot, read_slot;
} t384_block_ring_t;

/* Single V3F initializer. Both linkers reserve this NOLOAD region; neither
 * startup's .bss clear includes it. Payload lives in V5F DTCM, not here. */
typedef struct {
    uint32_t magic, version, clock_ms, v5f_booted, v5f_initialized;
    uint32_t v3f_frame_access_ok;
    uint32_t producer_sequence, producer_bank, consumer_bank, read_leased;
    union {
        t384_frame_bank_t banks[T384_FRAME_BANKS];
        t384_block_ring_t ring;
    };
    t384_frame_pipeline_stats_t pipeline;
    uint32_t snapshot_sequence;
    t384_frame_source_stats_t source;
    t384_module_file_status_t file;
    uint32_t rpc_request, rpc_ack, rpc_command, rpc_argument;
    uint32_t rpc_stream_active, rpc_cancel;
    int32_t rpc_result;
    uint32_t rpc_pointer;
    char rpc_id[16];
} t384_dualcore_shared_t;

typedef char t384_ipc_fits_1k[sizeof(t384_dualcore_shared_t) <= 1024u ? 1 : -1];

extern t384_dualcore_shared_t t384_dualcore_shared;
extern uint8_t t384_dualcore_frame[T384_CAPTURE_BUFFER_BYTES];
extern uint8_t t384_frame1_itcm[T384_FRAME1_ITCM_BYTES];
extern uint8_t t384_frame1_dtcm[T384_FRAME1_DTCM_BYTES];
extern uint8_t t384_frame1_code[T384_FRAME1_CODE_BYTES];
extern uint8_t t384_frame1_data[T384_FRAME1_DATA_BYTES];
#if T384_PIPELINE_PACKED_PICTURE && T384_NETWORK_ON_V5F
extern uint8_t t384_frame_shared_payload[T384_FRAME_SHARED_PAYLOAD_BYTES];
extern uint8_t t384_frame_shared_metadata[T384_FRAME_SHARED_METADATA_BYTES];
#endif

#if T384_PIPELINE_PACKED_PICTURE
#include "t384_packed_picture.h"
#if T384_NETWORK_ON_V5F
typedef char t384_picture_region_bounds[
    T384_CAPTURE_BUFFER_BYTES ==
        T384_FRAME_LOCAL_SLOT_COUNT * T384_PIPELINE_STORAGE_CHUNK_BYTES &&
    T384_FRAME_SHARED_PAYLOAD_BYTES ==
        T384_FRAME_SHARED_SLOT_COUNT * T384_PIPELINE_STORAGE_CHUNK_BYTES &&
    T384_FRAME_SHARED_METADATA_BYTES == T384_PIPELINE_SLOT_COUNT * 16u + 32u ? 1 : -1];
extern uint8_t t384_picture_expand_scratch[T384_PIPELINE_CHUNK_BYTES];
static inline uint8_t *t384_picture_slot_data(uint32_t slot)
{
    if (slot < T384_FRAME_LOCAL_SLOT_COUNT) {
        return t384_dualcore_frame +
            slot * T384_PIPELINE_STORAGE_CHUNK_BYTES;
    }
    return t384_frame_shared_payload +
        (slot - T384_FRAME_LOCAL_SLOT_COUNT) *
            T384_PIPELINE_STORAGE_CHUNK_BYTES;
}
static inline uint8_t *t384_picture_slot_metadata(void)
{
    return t384_frame_shared_metadata + 32u;
}
static inline void t384_picture_prepare_payload(t384_frame_chunk_view_t *view)
{
    t384_picture_expand(t384_picture_expand_scratch, view->data, view->length);
    view->data = t384_picture_expand_scratch;
}
#else
typedef char t384_picture_region_bounds[
    T384_CAPTURE_BUFFER_BYTES <= 216u * 1024u &&
    T384_FRAME1_DTCM_BYTES <= 18u * 1024u &&
    T384_FRAME1_CODE_BYTES <= 42u * 1024u &&
    T384_FRAME1_DATA_BYTES <= 60u * 1024u &&
    T384_PIPELINE_SLOT_COUNT * T384_PIPELINE_CHUNK_BYTES == T384_RAW16_FRAME_BYTES ? 1 : -1];
static inline uint8_t *t384_picture_slot_data(uint32_t slot)
{
    if (slot < 85u) return t384_dualcore_frame + slot * T384_PIPELINE_STORAGE_CHUNK_BYTES;
    slot -= 85u;
    if (slot < 7u) return t384_frame1_dtcm + slot * T384_PIPELINE_STORAGE_CHUNK_BYTES;
    slot -= 7u;
    if (slot < 16u) return t384_frame1_code + slot * T384_PIPELINE_STORAGE_CHUNK_BYTES;
    slot -= 16u;
    return t384_frame1_data + slot * T384_PIPELINE_STORAGE_CHUNK_BYTES;
}
/* Call only while owning the consumer lease; idle draining never expands. */
static inline void t384_picture_prepare_payload(t384_frame_chunk_view_t *view)
{
    uint8_t *out = t384_frame1_data + 20u * T384_PIPELINE_STORAGE_CHUNK_BYTES;
    t384_picture_expand(out, view->data, view->length);
    view->data = out;
}
#endif
#endif

/* All secondary boundaries are multiples of the 384 profile's 6144B chunk;
 * no DMA/COPY lease crosses a physical region. 256 uses only the first 96K. */
static inline uint8_t *t384_frame_bank_data(unsigned bank, uint32_t offset)
{
#if T384_NETWORK_ON_V5F
    if (bank == 0u) return (uint8_t *)(uintptr_t)(0x200C0300u + offset);
#else
    if (bank == 0u) return t384_dualcore_frame + offset;
#endif
    if (offset < T384_FRAME1_ITCM_BYTES) return t384_frame1_itcm + offset;
    offset -= T384_FRAME1_ITCM_BYTES;
    if (offset < T384_FRAME1_DTCM_BYTES) return t384_frame1_dtcm + offset;
    offset -= T384_FRAME1_DTCM_BYTES;
    if (offset < T384_FRAME1_CODE_BYTES) return t384_frame1_code + offset;
    return t384_frame1_data + offset - T384_FRAME1_CODE_BYTES;
}

static inline uint8_t *t384_frame_probe_region(unsigned region)
{
#if T384_NETWORK_ON_V5F
    /* The packed 640 ring has a shared-SRAM metadata header reserved for
     * probes. Other V5F profiles retain the small DTCM metadata bank. */
#if T384_PIPELINE_PACKED_PICTURE
    return t384_frame_shared_metadata +
        (region % T384_FRAME_PROBE_REGIONS) * 4u;
#else
    return (uint8_t *)(uintptr_t)(0x200FB000u +
        T384_PIPELINE_SLOT_COUNT * 16u +
        (region % T384_FRAME_PROBE_REGIONS) * 4u);
#endif
#else
    switch (region) {
    case 0u: return t384_dualcore_frame;
    case 1u: return t384_frame1_itcm;
    case 2u: return t384_frame1_dtcm;
    case 3u: return t384_frame1_code;
    default: return t384_frame1_data;
    }
#endif
}

void t384_dualcore_init(void);
void t384_dualcore_capture_task(void);

#endif
