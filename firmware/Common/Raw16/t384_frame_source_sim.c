#include "t384_frame_source.h"
#include "t384_module_files.h"

#if T384_FRAME_SOURCE_SIMULATOR

bool t384_module_file_port_pause(uint8_t **scratch, size_t *capacity)
{ (void)scratch; (void)capacity; return false; }
void t384_module_file_port_resume(void) {}
bool t384_module_file_port_tx(uint8_t byte) { (void)byte; return false; }
int t384_module_file_port_rx(void) { return -1; }

#include <stddef.h>
#include <string.h>

#if T384_FRAME_SOURCE_DMA_EQUIV && !defined(T384_HOST_SYNTAX_CHECK)
#include "ch32h417.h"
#include "ch32h417_dma.h"
#endif

#include "t384_frame_pipeline.h"
#include "t384_compiler.h"
#include "t384_raw16.h"
#include "t384_raw16_wire.h"
#include "t384_time.h"

#define T384_SIM_TARGET_BPS 7200000u
#define T384_SIM_TASK_CHUNK_BUDGET 8u

static t384_frame_source_stats_t source_stats;
static uint64_t byte_credit_x1000;
static uint32_t credit_updated_ms;
static uint32_t frame_sequence;
static uint32_t frame_offset;
static bool frame_open;
static bool dropping_frame;
static uint32_t fps_started_ms;
static uint32_t fps_frames;

#if T384_FRAME_SOURCE_DMA_EQUIV && !defined(T384_HOST_SYNTAX_CHECK)
/*
 * This buffer is a one-time deterministic scene seed, not a capture frame.
 * DMA repeats the 8-row seed for every pipeline chunk so the test consumes
 * the same fixed-size payload chunks without spending CPU cycles on every pixel.
 * The repeated scene is intentional: this build measures transport capacity,
 * while the real MINI2 DVP adapter will supply the actual bytes.
 */
static uint8_t dma_seed[T384_PIPELINE_CHUNK_BYTES] __attribute__((aligned(32)));
static bool dma_active;
static uint16_t dma_length;
static bool dma_end_of_frame;

#define T384_DMA_CHANNEL DMA1_Channel1
#define T384_DMA_FLAGS \
    (DMA1_FLAG_GL1 | DMA1_FLAG_TC1 | DMA1_FLAG_HT1 | DMA1_FLAG_TE1)

static void dma_source_init(void)
{
    for (uint32_t word = 0u;
         word < T384_PIPELINE_CHUNK_BYTES / sizeof(uint16_t); ++word) {
        const uint16_t value = t384_raw16_word(0u, word);
        dma_seed[word * 2u] = (uint8_t)(value >> 8);
        dma_seed[word * 2u + 1u] = (uint8_t)value;
    }

    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);
    DMA_DeInit(T384_DMA_CHANNEL);
    DMA_ClearFlag(DMA1, T384_DMA_FLAGS);

    DMA_InitTypeDef config;
    DMA_StructInit(&config);
    config.DMA_PeripheralBaseAddr = (uint32_t)(uintptr_t)dma_seed;
    config.DMA_DIR = DMA_DIR_PeripheralSRC;
    config.DMA_BufferSize = T384_PIPELINE_CHUNK_BYTES;
    config.DMA_PeripheralInc = DMA_PeripheralInc_Enable;
    config.DMA_MemoryInc = DMA_MemoryInc_Enable;
    config.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    config.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    config.DMA_Mode = DMA_Mode_Normal;
    config.DMA_Priority = DMA_Priority_VeryHigh;
    config.DMA_M2M = DMA_M2M_Enable;
    DMA_Init(T384_DMA_CHANNEL, &config);
    dma_active = false;
    dma_length = 0u;
    dma_end_of_frame = false;
}

static void dma_start(uint8_t *destination, uint16_t length, bool end_of_frame)
{
    DMA_Cmd(T384_DMA_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA1, T384_DMA_FLAGS);
    T384_DMA_CHANNEL->PADDR = (uint32_t)(uintptr_t)dma_seed;
    T384_DMA_CHANNEL->MADDR = (uint32_t)(uintptr_t)destination;
    T384_DMA_CHANNEL->CNTR = length;
    dma_length = length;
    dma_end_of_frame = end_of_frame;
    T384_MEMORY_BARRIER();
    DMA_Cmd(T384_DMA_CHANNEL, ENABLE);
    dma_active = true;
}

/* 0 = still running, 1 = complete, -1 = transfer error. */
static int dma_poll(void)
{
    if (DMA_GetFlagStatus(DMA1, DMA1_FLAG_TE1) == SET) {
        DMA_Cmd(T384_DMA_CHANNEL, DISABLE);
        DMA_ClearFlag(DMA1, T384_DMA_FLAGS);
        return -1;
    }
    if (DMA_GetFlagStatus(DMA1, DMA1_FLAG_TC1) != SET) {
        return 0;
    }
    DMA_Cmd(T384_DMA_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA1, T384_DMA_FLAGS);
    return 1;
}
#endif

bool t384_frame_source_init(void)
{
    memset(&source_stats, 0, sizeof(source_stats));
    source_stats.initialized = 1u;
#if T384_FRAME_SOURCE_DMA_EQUIV && !defined(T384_HOST_SYNTAX_CHECK)
    source_stats.synthetic = 0u;
#else
    source_stats.synthetic = 1u;
#endif
    source_stats.target_bps = T384_SIM_TARGET_BPS;
    source_stats.stream_ready = 1u;
    source_stats.frame_mode = T384_FRAME_MODE_TPD_Y16;
    source_stats.pixel_format = T384_FRAME_PIXEL_FORMAT_Y16_BE;
    byte_credit_x1000 = 0u;
    credit_updated_ms = t384_millis();
    fps_started_ms = credit_updated_ms;
    fps_frames = 0u;
    frame_sequence = 0u;
    frame_offset = 0u;
    frame_open = false;
    dropping_frame = false;
#if T384_FRAME_SOURCE_DMA_EQUIV && !defined(T384_HOST_SYNTAX_CHECK)
    dma_source_init();
#endif
    return true;
}

const char *t384_frame_source_name(void)
{
#if T384_FRAME_SOURCE_DMA_EQUIV && !defined(T384_HOST_SYNTAX_CHECK)
    return "dma-equivalent-dvp-source-v1";
#else
    return "synthetic-dvp-adapter-v1";
#endif
}

bool t384_frame_source_stream_ready(void)
{
    return true;
}

uint16_t t384_frame_source_pixel_format(void)
{
    return T384_FRAME_PIXEL_FORMAT_Y16_BE;
}

uint16_t t384_frame_source_mode_flags(void)
{
    return T384_CHUNK_FLAG_TPD_Y16;
}

static void finish_physical_frame(uint32_t now)
{
    ++source_stats.frames;
    ++fps_frames;
    if (dropping_frame) {
        ++source_stats.dropped_frames;
    } else {
        ++source_stats.published_frames;
    }
    ++frame_sequence;
    frame_offset = 0u;
    dropping_frame = false;

    const uint32_t elapsed = (uint32_t)(now - fps_started_ms);
    if (elapsed >= 1000u) {
        source_stats.source_fps_x1000 =
            (uint32_t)(((uint64_t)fps_frames * 1000000u) / elapsed);
        fps_started_ms = now;
        fps_frames = 0u;
    }
}

void t384_frame_source_task(void)
{
    if (source_stats.initialized == 0u) {
        return;
    }

    const uint32_t now = t384_millis();
    const uint32_t elapsed = (uint32_t)(now - credit_updated_ms);
    if (elapsed != 0u) {
        byte_credit_x1000 += (uint64_t)elapsed * T384_SIM_TARGET_BPS;
        credit_updated_ms = now;
        const uint64_t credit_limit = (uint64_t)T384_RAW16_FRAME_BYTES * 1000u;
        if (byte_credit_x1000 > credit_limit) {
            byte_credit_x1000 = credit_limit;
            ++source_stats.schedule_overruns;
        }
    }

    for (unsigned emitted = 0u; emitted < T384_SIM_TASK_CHUNK_BUDGET; ++emitted) {
#if T384_FRAME_SOURCE_DMA_EQUIV && !defined(T384_HOST_SYNTAX_CHECK)
        if (dma_active) {
            const int dma_status = dma_poll();
            if (dma_status == 0) {
                return;
            }

            const uint16_t completed_length = dma_length;
            const bool completed_end = dma_end_of_frame;
            dma_active = false;
            T384_MEMORY_BARRIER();
            if (dma_status < 0 ||
                !t384_frame_pipeline_commit_write(completed_length,
                                                   completed_end)) {
                t384_frame_pipeline_abort_frame();
                frame_open = false;
                dropping_frame = true;
            }

            frame_offset += completed_length;
            if (frame_offset == T384_RAW16_FRAME_BYTES) {
                finish_physical_frame(now);
                frame_open = false;
            }
        }
#endif

        uint32_t chunk_length = T384_RAW16_FRAME_BYTES - frame_offset;
        if (chunk_length > T384_PIPELINE_CHUNK_BYTES) {
            chunk_length = T384_PIPELINE_CHUNK_BYTES;
        }
        const uint64_t chunk_cost = (uint64_t)chunk_length * 1000u;
        if (byte_credit_x1000 < chunk_cost) {
            break;
        }
        byte_credit_x1000 -= chunk_cost;

        if (!frame_open && frame_offset == 0u) {
#if T384_FRAME_SOURCE_DMA_EQUIV && !defined(T384_HOST_SYNTAX_CHECK)
            const uint16_t source_flags = T384_CHUNK_FLAG_TPD_Y16;
#else
            const uint16_t source_flags =
                T384_CHUNK_FLAG_SYNTHETIC | T384_CHUNK_FLAG_TPD_Y16;
#endif
            if (!t384_frame_pipeline_begin_frame(
                    frame_sequence, now, source_flags)) {
                dropping_frame = true;
            } else {
                frame_open = true;
            }
        }

        if (!dropping_frame) {
            uint8_t *destination = NULL;
            uint16_t capacity = 0u;
            if (!t384_frame_pipeline_acquire_write(&destination, &capacity)) {
                /* Backpressure pauses this physical frame; do not publish a
                 * partial frame that the browser can never complete. */
                byte_credit_x1000 += chunk_cost;
                break;
            }
            if (chunk_length > capacity) {
                t384_frame_pipeline_abort_frame();
                frame_open = false;
                dropping_frame = true;
#if T384_FRAME_SOURCE_DMA_EQUIV && !defined(T384_HOST_SYNTAX_CHECK)
            } else {
                dma_start(destination, (uint16_t)chunk_length,
                          frame_offset + chunk_length == T384_RAW16_FRAME_BYTES);
                return;
            }
#else
            } else if (t384_raw16_fill(frame_sequence, frame_offset,
                                       destination, chunk_length) != chunk_length ||
                       !t384_frame_pipeline_commit_write(
                           (uint16_t)chunk_length,
                           frame_offset + chunk_length == T384_RAW16_FRAME_BYTES)) {
                t384_frame_pipeline_abort_frame();
                frame_open = false;
                dropping_frame = true;
            }
#endif
        }

#if !(T384_FRAME_SOURCE_DMA_EQUIV && !defined(T384_HOST_SYNTAX_CHECK))
        frame_offset += chunk_length;
        if (frame_offset == T384_RAW16_FRAME_BYTES) {
            finish_physical_frame(now);
            frame_open = false;
        }
#else
        if (dropping_frame) {
            frame_offset += chunk_length;
            if (frame_offset == T384_RAW16_FRAME_BYTES) {
                finish_physical_frame(now);
                frame_open = false;
            }
        }
#endif
    }
}

void t384_frame_source_get_stats(t384_frame_source_stats_t *out)
{
    if (out != NULL) {
        *out = source_stats;
    }
}

#endif
