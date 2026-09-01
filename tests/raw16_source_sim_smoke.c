#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "t384_frame_pipeline.h"
#include "t384_frame_source.h"

static uint32_t fake_millis;

uint32_t t384_millis(void)
{
    return fake_millis;
}

static void drain_pipeline(void)
{
    t384_frame_chunk_view_t chunk;
    while (t384_frame_pipeline_peek(&chunk)) {
        assert(chunk.data != NULL);
        assert(chunk.length != 0u);
        t384_frame_pipeline_release();
    }
}

int main(void)
{
    fake_millis = 0u;
    t384_frame_pipeline_init();
    assert(t384_frame_source_init());
    assert(t384_frame_source_name() != NULL);

    for (fake_millis = 1u; fake_millis <= 1000u; ++fake_millis) {
        t384_frame_source_task();
        drain_pipeline();
    }

    t384_frame_source_stats_t source;
    t384_frame_pipeline_stats_t pipeline;
    t384_frame_source_get_stats(&source);
    t384_frame_pipeline_get_stats(&pipeline);
    assert(source.initialized == 1u);
    assert(source.synthetic == 1u);
    assert(source.target_bps == 7200000u);
    assert(source.frames == 32u);
    assert(source.published_frames == source.frames);
    assert(source.dropped_frames == 0u);
    assert(source.schedule_overruns == 0u);
    assert(pipeline.frames_completed == source.published_frames);
    assert(pipeline.frames_aborted == 0u);
    assert(pipeline.queued_chunks == 0u);

    fake_millis = 0u;
    t384_frame_pipeline_init();
    assert(t384_frame_source_init());
    for (fake_millis = 1u; fake_millis <= 100u; ++fake_millis) {
        t384_frame_source_task();
    }
    t384_frame_source_get_stats(&source);
    t384_frame_pipeline_get_stats(&pipeline);
    assert(source.dropped_frames == 0u);
    assert(pipeline.acquire_no_slot > 0u);
    assert(pipeline.high_water_chunks == T384_PIPELINE_SLOT_COUNT);

    for (fake_millis = 101u; fake_millis <= 2000u; ++fake_millis) {
        t384_frame_source_task();
        drain_pipeline();
    }
    t384_frame_source_get_stats(&source);
    t384_frame_pipeline_get_stats(&pipeline);
    assert(source.published_frames > 0u);
    assert(source.dropped_frames == 0u);
    assert(pipeline.frames_completed == source.published_frames);

    puts("T384 time-driven synthetic capture adapter smoke passed");
    return 0;
}
