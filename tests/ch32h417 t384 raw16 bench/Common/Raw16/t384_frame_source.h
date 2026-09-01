#ifndef T384_FRAME_SOURCE_H
#define T384_FRAME_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Hardware-build isolation mode.  The V3F bench uses DMA1 memory-to-memory
 * transfers to remove the CPU pixel-generation cost while keeping the exact
 * source->pipeline->NCM path.  A real MINI2 adapter will replace the whole
 * source implementation behind this header; downstream ownership and wire
 * code remain unchanged.
 */
#ifndef T384_FRAME_SOURCE_DMA_EQUIV
#define T384_FRAME_SOURCE_DMA_EQUIV 1
#endif

typedef struct {
    uint32_t initialized;
    uint32_t synthetic;
    uint32_t target_bps;
    uint32_t frames;
    uint32_t published_frames;
    uint32_t dropped_frames;
    uint32_t schedule_overruns;
    uint32_t source_fps_x1000;
} t384_frame_source_stats_t;

/*
 * Stable capture-adapter boundary.  The simulator implements these symbols
 * today.  A MINI2 DVP adapter will implement the same API and feed the same
 * t384_frame_pipeline producer functions; NCM/HTTP/browser code stays intact.
 * The adapter owns ISR/task synchronization: the recommended path is for the
 * DVP ISR to publish only source-local DMA completion flags and for source_task
 * to perform serialized pipeline metadata calls.  Never commit before DMA is
 * complete or abort while DMA can still write the leased slot.
 */
bool t384_frame_source_init(void);
void t384_frame_source_task(void);
const char *t384_frame_source_name(void);
void t384_frame_source_get_stats(t384_frame_source_stats_t *out);

#endif
