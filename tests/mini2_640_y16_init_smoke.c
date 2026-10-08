/* Reuse the existing independent UART/CRC fixture, but exercise the real
 * 640 Y16 initialization and LE->BE block copy rather than native Picture. */
#define T384_640_Y16_STREAM_ENABLED 1u
#define main native_picture_fixture_main
#include "mini2_stream_init_smoke.c"
#undef main

int main(void)
{
#if !T384_PIPELINE_640_Y16
#error "Requires the dual-core V5F 640 Y16 build"
#endif
    static uint8_t input[T384_MINI2_DMA_BLOCK_BYTES] __attribute__((aligned(4)));
    static uint8_t output[T384_MINI2_DMA_BLOCK_BYTES] __attribute__((aligned(4)));
    for (unsigned test = 0u; test < 8u; ++test) {
        now = reply_at = detector_ready_at = digital_ready_at = 0u;
        command_length = reply_length = reply_offset = 0u;
        detector_sets = mode_sets = persist_sets = digital_queries = 0u;
        scenario = test == 2u ? LOST_TPD_ACK : test == 3u ? TPD_REJECT :
                   test == 4u ? MODE_QUERY_TIMEOUT : test == 5u ? MODE_MISMATCH : NORMAL;
        mode = test == 1u ? 1u : 0u;
        native_yuv_format = 2u;
        memset((void *)&source_stats, 0, sizeof(source_stats));
        source_stats.mini2_pn_valid = source_stats.mini2_firmware_version_valid = 1u;
        strcpy((char *)source_stats.mini2_pn, test == 6u ? "WN2384" : "TIFSC640");
        strcpy((char *)source_stats.mini2_firmware_version, "01.00.01.03");
        source_stats.mini2_query_stream_mode_valid = test == 7u ? 0u : 1u;
        source_stats.mini2_query_stream_mode_0x85 = mode;
        mini2_configure_stream();
        const bool ready = test < 3u;
        assert((source_stats.stream_ready != 0u) == ready);
        assert(detector_sets == 0u && digital_queries == 0u && persist_sets == 0u);
        assert(mode_sets == (test == 1u || test >= 6u ? 0u : 1u));
        if (!ready) {
            assert(source_stats.frame_mode == T384_FRAME_MODE_UNKNOWN);
            continue;
        }
        assert(source_stats.frame_mode == T384_FRAME_MODE_TPD_Y16);
        assert(source_stats.pixel_format == T384_FRAME_PIXEL_FORMAT_Y16_BE);
        for (unsigned i = 0u; i < sizeof(input); i += 2u) {
            const uint16_t value = (uint16_t)(0x53D0u + i / 2u);
            input[i] = (uint8_t)value;
            input[i + 1u] = (uint8_t)(value >> 8);
        }
        assert(mini2_copy_dma_block(output, input));
        for (unsigned i = 0u; i < sizeof(input); i += 2u) {
            const uint16_t value = (uint16_t)(0x53D0u + i / 2u);
            assert(input[i] == (uint8_t)value && input[i + 1u] == (uint8_t)(value >> 8));
            assert(output[i] == (uint8_t)(value >> 8) && output[i + 1u] == (uint8_t)value);
        }
    }
    puts("640 Y16: identity/mode readback, warm reset, LE->BE and no rate/persist writes");
    return 0;
}
