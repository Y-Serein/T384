#ifndef T384_CAMERA_H
#define T384_CAMERA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct t384_camera_stats
{
    uint32_t init_attempts;
    uint32_t init_ok;
    uint32_t init_fail;
    uint32_t sensor_mid;
    uint32_t sensor_pid;
    uint32_t requests;
    uint32_t starts;
    uint32_t frame_starts;
    uint32_t row_chunks;
    uint32_t frame_done_irqs;
    uint32_t stop_frame_irqs;
    uint32_t frame_ends;
    uint32_t fifo_overflows;
    uint32_t frames;
    uint32_t published_frames;
    uint32_t bad_frames;
    uint32_t overflows;
    uint32_t timeouts;
    uint32_t dropped_ready;
    uint32_t dropped_no_slot;
    uint32_t bytes;
    uint32_t last_frame_bytes;
    uint32_t max_frame_bytes;
    uint32_t source_fps_x1000;
    uint32_t active;
    uint32_t ready;
    uint32_t leased;
} t384_camera_stats_t;

bool t384_camera_init(void);
void t384_camera_task(void);
bool t384_camera_is_initialized(void);
bool t384_camera_trigger(void);
bool t384_camera_acquire_frame(const uint8_t **data, uint32_t *length);
void t384_camera_release_frame(void);
void t384_camera_get_stats(t384_camera_stats_t *out);

#endif
