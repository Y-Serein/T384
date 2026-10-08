#ifndef T384_640_MODE_PROBE_H
#define T384_640_MODE_PROBE_H

#include <stdbool.h>
#include <stdint.h>
#include "t384_raw16_roi.h"

/* V5F-local evidence, deliberately outside the almost-full 1 KiB IPC ABI.
 * A mode reply or physical frame count does not establish radiometry. */
enum {
    T384_MODE_PROBE_DISABLED,
    T384_MODE_PROBE_SKIPPED,
    T384_MODE_PROBE_OBSERVING,
    T384_MODE_PROBE_RESTORING,
    T384_MODE_PROBE_VERIFYING,
    T384_MODE_PROBE_RESTORED,
    T384_MODE_PROBE_RESTORE_FAILED,
    T384_MODE_PROBE_PENDING
};

typedef struct {
    uint32_t state;
    uint32_t set_result, query_result, mode;
    uint32_t restore_result, restore_query_result, restore_mode;
    uint32_t baseline_yuv, restore_yuv;
    uint32_t observation_ms;
    uint32_t frames, complete_frames, bad_frames, fifo_overflows;
    uint32_t last_rows, last_bytes;
    uint32_t prefix_valid;
    uint8_t prefix[32];
    t384_raw16_roi_snapshot_t roi;
    uint32_t picture_complete_frames, picture_bad_frames;
    uint32_t picture_consecutive_frames;
} t384_640_mode_probe_stats_t;

/* Available only on the enabled 640 V5F data-plane build. */
bool t384_640_mode_probe_get_stats(t384_640_mode_probe_stats_t *out);

#endif
