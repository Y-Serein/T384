#ifndef T384_FRAME_SOURCE_H
#define T384_FRAME_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Product builds use the passive MINI2 DVP bring-up adapter.  The simulator
 * remains available only for host tests and explicit transport regression
 * builds; it must never be selected silently when real capture is absent.
 */
#ifndef T384_FRAME_SOURCE_SIMULATOR
#define T384_FRAME_SOURCE_SIMULATOR 0
#endif

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
    uint32_t stream_ready;
    uint32_t capture_active;
    uint32_t dvp_frame_starts;
    uint32_t dvp_row_events;
    uint32_t dvp_frame_done_irqs;
    uint32_t dvp_stop_frame_irqs;
    uint32_t dvp_frame_ends;
    uint32_t dvp_fifo_overflows;
    uint32_t dvp_orphan_rows;
    uint32_t dvp_bad_frames;
    uint32_t dvp_last_frame_rows;
    uint32_t dvp_last_frame_bytes;
    uint64_t dvp_observed_bytes;
    uint32_t mini2_control_attempts;
    uint32_t mini2_control_tx_bytes;
    uint32_t mini2_control_ack_valid;
    uint32_t mini2_control_ack_status;
    uint32_t mini2_control_ack_timeout;
    uint32_t mini2_control_ack_bad;
    uint32_t mini2_control_digital_off_status;
    uint32_t mini2_control_analog_off_status;
    uint32_t mini2_control_detector30_status;
    uint32_t mini2_control_dvp30_status;
    uint32_t mini2_query_detector_valid;
    uint32_t mini2_query_detector_status;
    uint32_t mini2_query_detector_fps;
    uint32_t mini2_query_digital_valid;
    uint32_t mini2_query_digital_status;
    uint32_t mini2_query_digital_enabled;
    uint32_t mini2_query_digital_format;
    uint32_t mini2_query_digital_fps;
    char mini2_device_name[16];
    char mini2_firmware_version[12];
    uint32_t mini2_device_name_valid;
    uint32_t mini2_firmware_version_valid;
} t384_frame_source_stats_t;

/*
 * Stable capture-adapter boundary.  The simulator implements these symbols
 * today.  A MINI2 DVP adapter will implement the same API and feed the same
 * t384_frame_pipeline producer functions; NCM/HTTP/browser code stays intact.
 * The current WN2256 bring-up adapter completes one row DMA sink and commits
 * bounded 8-row chunks in the DVP ISR.  Never commit before the row DMA is
 * complete or abort while DMA can still write the leased slot.  A future
 * higher-throughput adapter may move metadata calls to source_task, but must
 * preserve the same single-producer ownership contract.
 */
bool t384_frame_source_init(void);
void t384_frame_source_task(void);
const char *t384_frame_source_name(void);
bool t384_frame_source_stream_ready(void);
void t384_frame_source_get_stats(t384_frame_source_stats_t *out);

#endif
