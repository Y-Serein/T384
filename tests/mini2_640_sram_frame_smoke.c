/* Production codec/ring: capture a whole frame without any consumer. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "t384_dualcore.h"

#if !T384_PIPELINE_PACKED_PICTURE
#error "Requires dual-core 640 profile"
#endif
t384_dualcore_shared_t t384_dualcore_shared;
uint8_t t384_dualcore_frame[T384_CAPTURE_BUFFER_BYTES] __attribute__((aligned(32)));
#if T384_PIPELINE_PACKED_PICTURE && T384_NETWORK_ON_V5F
uint8_t t384_frame_shared_payload[T384_FRAME_SHARED_PAYLOAD_BYTES]
    __attribute__((aligned(32)));
uint8_t t384_frame_shared_metadata[T384_FRAME_SHARED_METADATA_BYTES]
    __attribute__((aligned(32)));
#endif
uint8_t t384_frame1_itcm[T384_FRAME1_ITCM_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_dtcm[T384_FRAME1_DTCM_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_code[T384_FRAME1_CODE_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_data[T384_FRAME1_DATA_BYTES] __attribute__((aligned(32)));
void t384_dualcore_init(void) { memset(&t384_dualcore_shared, 0, sizeof(t384_dualcore_shared)); }
static uint8_t pixels[T384_PIPELINE_CHUNK_BYTES] __attribute__((aligned(32)));
static void fill(unsigned block)
{
    for (unsigned i = 0u; i < sizeof(pixels); i += 4u) {
        pixels[i] = (uint8_t)(40u + block);
        pixels[i+1u] = (uint8_t)(i / 2u + block);
        pixels[i+2u] = (uint8_t)(200u + block);
        pixels[i+3u] = (uint8_t)(i / 2u + block + 1u);
    }
}
int main(void)
{
    static const uint8_t orders[4][8] = {
        {40,90,200,180,40,17,200,253}, {200,90,40,180,200,17,40,253},
        {90,40,180,200,17,40,253,200}, {90,200,180,40,17,200,253,40}
    };
    uint8_t in[8] __attribute__((aligned(4)));
    uint8_t out[8] __attribute__((aligned(4)));
    uint8_t packed[40] __attribute__((aligned(4)));
    for (unsigned f = 0u; f < 4u; ++f) {
        memcpy(in, orders[f], sizeof(in));
        memset(packed, 0xA5, sizeof(packed));
        assert(t384_picture_pack(packed, in, sizeof(in), f));
        t384_picture_expand(out, packed, sizeof(out));
        assert(memcmp(out, orders[0], sizeof(out)) == 0);
        assert(packed[36] == 0xA5 && packed[39] == 0xA5);
        in[f < 2u ? 4u : 5u] ^= 1u;
        assert(!t384_picture_pack(packed, in, sizeof(in), f));
    }
    t384_frame_pipeline_init();
    for (unsigned frame = 0u; frame < 3u; ++frame) {
        assert(t384_frame_pipeline_begin_frame(frame, 0u, T384_CHUNK_FLAG_PICTURE_UYVY));
        for (unsigned block = 0u; block < 128u; ++block) {
            uint8_t *destination; uint16_t capacity;
            assert(t384_frame_pipeline_acquire_write(&destination, &capacity));
            assert(capacity == T384_PIPELINE_STORAGE_CHUNK_BYTES);
            fill(block);
            assert(t384_picture_pack(destination, pixels, sizeof(pixels), 0u));
            assert(t384_frame_pipeline_commit_write(sizeof(pixels), block == 127u));
        }
        t384_frame_pipeline_stats_t stats;
        t384_frame_pipeline_get_stats(&stats);
        assert(stats.queued_chunks == 128u && stats.frames_aborted == 0u);
        assert(stats.frames_completed == frame + 1u);
        for (unsigned block = 0u; block < 128u; ++block) {
            t384_frame_chunk_view_t view, second;
            assert(t384_frame_pipeline_peek(&view));
            assert(!t384_frame_pipeline_peek(&second));
            assert(view.frame_offset == block * sizeof(pixels));
            assert(view.frame_sequence == frame);
            assert(((view.flags & T384_CHUNK_FLAG_FRAME_END) != 0u) == (block == 127u));
            t384_picture_prepare_payload(&view);
            fill(block);
            assert(memcmp(view.data, pixels, sizeof(pixels)) == 0);
            t384_frame_pipeline_release();
        }
        assert(t384_frame_pipeline_empty());
    }
    puts("640 cold-consumer full frames, SRAM boundaries, word codec and leases passed");
    return 0;
}
