#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "t384_raw16.h"
#include "t384_raw16_roi.h"

static void put_be16(uint8_t *row, uint32_t x, uint16_t value)
{
    row[x * 2u] = (uint8_t)(value >> 8);
    row[x * 2u + 1u] = (uint8_t)value;
}

int main(void)
{
    uint8_t row[T384_RAW16_WIDTH * 2u];
    t384_raw16_roi_accumulator_t accumulator;
    t384_raw16_roi_snapshot_t snapshot;
    t384_raw16_roi_metrics_t metrics;
    const uint32_t roi_x = (T384_RAW16_WIDTH - T384_RAW16_ROI_WIDTH) / 2u;
    const uint32_t roi_y = (T384_RAW16_HEIGHT - T384_RAW16_ROI_HEIGHT) / 2u;

    t384_raw16_roi_reset(&accumulator);
    for (uint32_t y = 0u; y < T384_RAW16_HEIGHT; ++y) {
        memset(row, 0xA5, sizeof(row));
        if (y >= roi_y && y < roi_y + T384_RAW16_ROI_HEIGHT) {
            for (uint32_t x = 0u; x < T384_RAW16_ROI_WIDTH; ++x) {
                put_be16(row, roi_x + x,
                         (uint16_t)((y - roi_y) * T384_RAW16_ROI_WIDTH + x));
            }
        }
        t384_raw16_roi_add_be16_row(&accumulator, y, row, sizeof(row));
    }
    t384_raw16_roi_snapshot(&accumulator, 41u, true, false, &snapshot);
    t384_raw16_roi_calculate(&snapshot, &metrics);
    assert(snapshot.valid == 1u);
    assert(snapshot.frame_sequence == 41u);
    assert(snapshot.pipeline_published == 0u);
    assert(snapshot.sample_count == 256u);
    assert(snapshot.sum == 32640u);
    assert(snapshot.sum_squares == 5559680u);
    assert(snapshot.minimum == 0u);
    assert(snapshot.maximum == 255u);
    assert(snapshot.le_sum == 8355840u);
    assert(snapshot.le_sum_squares == 364359188480ULL);
    assert(snapshot.le_minimum == 0u);
    assert(snapshot.le_maximum == 65280u);
    assert(metrics.mean_raw_x100 == 12750u);
    assert(metrics.stddev_raw_x100 == 7390u);
    assert(metrics.le_mean_raw_x100 == 3264000u);
    assert(metrics.le_stddev_raw_x100 == 1891847u);

    t384_raw16_roi_snapshot(&accumulator, 42u, false, true, &snapshot);
    t384_raw16_roi_calculate(&snapshot, &metrics);
    assert(snapshot.valid == 0u);
    assert(snapshot.frame_sequence == 42u);
    assert(snapshot.pipeline_published == 1u);
    assert(snapshot.sample_count == 0u);
    assert(metrics.mean_raw_x100 == 0u);
    assert(metrics.stddev_raw_x100 == 0u);

    puts("T384 RAW16BE center ROI statistics smoke passed");
    return 0;
}
