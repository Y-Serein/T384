#ifndef T384_RAW16_ROI_H
#define T384_RAW16_ROI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define T384_RAW16_ROI_WIDTH 16u
#define T384_RAW16_ROI_HEIGHT 16u
#define T384_RAW16_ROI_SAMPLES \
    (T384_RAW16_ROI_WIDTH * T384_RAW16_ROI_HEIGHT)

typedef struct {
    uint32_t sample_count;
    uint32_t sum;
    uint64_t sum_squares;
    uint16_t minimum;
    uint16_t maximum;
    uint32_t le_sum;
    uint64_t le_sum_squares;
    uint16_t le_minimum;
    uint16_t le_maximum;
} t384_raw16_roi_accumulator_t;

typedef struct {
    uint32_t valid;
    uint32_t frame_sequence;
    uint32_t pipeline_published;
    uint32_t sample_count;
    uint32_t sum;
    uint64_t sum_squares;
    uint16_t minimum;
    uint16_t maximum;
    uint32_t le_sum;
    uint64_t le_sum_squares;
    uint16_t le_minimum;
    uint16_t le_maximum;
} t384_raw16_roi_snapshot_t;

typedef struct {
    uint32_t mean_raw_x100;
    uint32_t stddev_raw_x100;
    uint32_t le_mean_raw_x100;
    uint32_t le_stddev_raw_x100;
} t384_raw16_roi_metrics_t;

void t384_raw16_roi_reset(t384_raw16_roi_accumulator_t *accumulator);
/* Accumulate both interpretations of each unchanged DVP byte pair. */
void t384_raw16_roi_add_be16_row(t384_raw16_roi_accumulator_t *accumulator,
                                 uint32_t row_index,
                                 const uint8_t *row,
                                 size_t row_bytes);
void t384_raw16_roi_snapshot(const t384_raw16_roi_accumulator_t *accumulator,
                             uint32_t frame_sequence,
                             bool capture_complete,
                             bool pipeline_published,
                             t384_raw16_roi_snapshot_t *snapshot);
void t384_raw16_roi_calculate(const t384_raw16_roi_snapshot_t *snapshot,
                              t384_raw16_roi_metrics_t *metrics);

#endif
