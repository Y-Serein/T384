#include "t384_raw16_roi.h"

#include <string.h>

#include "t384_raw16.h"

#if T384_RAW16_WIDTH < T384_RAW16_ROI_WIDTH
#error "RAW16 frame width is smaller than the diagnostic ROI"
#endif

#if T384_RAW16_HEIGHT < T384_RAW16_ROI_HEIGHT
#error "RAW16 frame height is smaller than the diagnostic ROI"
#endif

static uint64_t integer_sqrt(uint64_t value)
{
    uint64_t result = 0u;
    uint64_t bit = (uint64_t)1u << 62;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

void t384_raw16_roi_reset(t384_raw16_roi_accumulator_t *accumulator)
{
    if (accumulator == NULL) {
        return;
    }
    memset(accumulator, 0, sizeof(*accumulator));
    accumulator->minimum = UINT16_MAX;
    accumulator->le_minimum = UINT16_MAX;
}

static void add_row(t384_raw16_roi_accumulator_t *accumulator,
                                 uint32_t row_index,
                                 const uint8_t *row,
                                 size_t row_bytes,
                                 bool input_little_endian)
{
    const uint32_t roi_x = (T384_RAW16_WIDTH - T384_RAW16_ROI_WIDTH) / 2u;
    const uint32_t roi_y = (T384_RAW16_HEIGHT - T384_RAW16_ROI_HEIGHT) / 2u;
    if (accumulator == NULL || row == NULL ||
        row_bytes < (size_t)T384_RAW16_WIDTH * 2u || row_index < roi_y ||
        row_index >= roi_y + T384_RAW16_ROI_HEIGHT) {
        return;
    }

    for (uint32_t x = roi_x; x < roi_x + T384_RAW16_ROI_WIDTH; ++x) {
        const uint32_t offset = x * 2u;
        const uint8_t high = row[offset + (input_little_endian ? 1u : 0u)];
        const uint8_t low = row[offset + (input_little_endian ? 0u : 1u)];
        const uint16_t sample =
            ((uint16_t)high << 8) | low;
        const uint16_t le_sample =
            ((uint16_t)low << 8) | high;
        accumulator->sum += sample;
        accumulator->sum_squares += (uint64_t)sample * sample;
        if (sample < accumulator->minimum) {
            accumulator->minimum = sample;
        }
        if (sample > accumulator->maximum) {
            accumulator->maximum = sample;
        }
        accumulator->le_sum += le_sample;
        accumulator->le_sum_squares += (uint64_t)le_sample * le_sample;
        if (le_sample < accumulator->le_minimum) {
            accumulator->le_minimum = le_sample;
        }
        if (le_sample > accumulator->le_maximum) {
            accumulator->le_maximum = le_sample;
        }
        ++accumulator->sample_count;
    }
}

void t384_raw16_roi_add_be16_row(t384_raw16_roi_accumulator_t *accumulator,
                                 uint32_t row_index,
                                 const uint8_t *row,
                                 size_t row_bytes)
{
    add_row(accumulator, row_index, row, row_bytes, false);
}

void t384_raw16_roi_add_le16_row(t384_raw16_roi_accumulator_t *accumulator,
                                 uint32_t row_index,
                                 const uint8_t *row,
                                 size_t row_bytes)
{
    add_row(accumulator, row_index, row, row_bytes, true);
}

void t384_raw16_roi_snapshot(const t384_raw16_roi_accumulator_t *accumulator,
                             uint32_t frame_sequence,
                             bool capture_complete,
                             bool pipeline_published,
                             t384_raw16_roi_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->frame_sequence = frame_sequence;
    snapshot->pipeline_published = pipeline_published ? 1u : 0u;
    if (accumulator == NULL || !capture_complete ||
        accumulator->sample_count != T384_RAW16_ROI_SAMPLES) {
        return;
    }
    snapshot->valid = 1u;
    snapshot->sample_count = accumulator->sample_count;
    snapshot->sum = accumulator->sum;
    snapshot->sum_squares = accumulator->sum_squares;
    snapshot->minimum = accumulator->minimum;
    snapshot->maximum = accumulator->maximum;
    snapshot->le_sum = accumulator->le_sum;
    snapshot->le_sum_squares = accumulator->le_sum_squares;
    snapshot->le_minimum = accumulator->le_minimum;
    snapshot->le_maximum = accumulator->le_maximum;
}

void t384_raw16_roi_calculate(const t384_raw16_roi_snapshot_t *snapshot,
                              t384_raw16_roi_metrics_t *metrics)
{
    if (metrics == NULL) {
        return;
    }
    memset(metrics, 0, sizeof(*metrics));
    if (snapshot == NULL || snapshot->valid == 0u ||
        snapshot->sample_count == 0u) {
        return;
    }

    const uint64_t count = snapshot->sample_count;
    metrics->mean_raw_x100 =
        (uint32_t)(((uint64_t)snapshot->sum * 100u + count / 2u) / count);

    const uint64_t squared_sum = (uint64_t)snapshot->sum * snapshot->sum;
    const uint64_t scaled_squares = count * snapshot->sum_squares;
    const uint64_t variance_numerator =
        scaled_squares >= squared_sum ? scaled_squares - squared_sum : 0u;
    const uint64_t scaled_root = integer_sqrt(variance_numerator * 10000u);
    metrics->stddev_raw_x100 =
        (uint32_t)((scaled_root + count / 2u) / count);

    metrics->le_mean_raw_x100 =
        (uint32_t)(((uint64_t)snapshot->le_sum * 100u + count / 2u) /
                   count);
    const uint64_t le_squared_sum =
        (uint64_t)snapshot->le_sum * snapshot->le_sum;
    const uint64_t le_scaled_squares = count * snapshot->le_sum_squares;
    const uint64_t le_variance_numerator =
        le_scaled_squares >= le_squared_sum
            ? le_scaled_squares - le_squared_sum
            : 0u;
    const uint64_t le_scaled_root =
        integer_sqrt(le_variance_numerator * 10000u);
    metrics->le_stddev_raw_x100 =
        (uint32_t)((le_scaled_root + count / 2u) / count);
}
