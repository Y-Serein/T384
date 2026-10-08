/* Boot-only 640 mode-1 observation must restore native Picture before HTTP.
 * The fixture executes the production UART state machine and DVP ISR with
 * fake registers; it performs no device I/O or persistent MINI2 operation. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* This fixture protects the earlier temporary probe/Picture rollback. */
#define T384_640_Y16_STREAM_ENABLED 0u

#include "ch32h417.h"

static DVP_TypeDef fake_dvp;
static bool irq_enabled;
#undef DVP
#define DVP (&fake_dvp)
#define NVIC_DisableIRQ(irq) ((void)(irq), irq_enabled = false)
#define NVIC_EnableIRQ(irq) ((void)(irq), irq_enabled = true)
#define NVIC_SetPriority(irq, priority) ((void)(irq), (void)(priority))

#include "../firmware/Common/Raw16/t384_frame_source_mini2.c"

#if T384_DUALCORE
#include "t384_dualcore.h"
t384_dualcore_shared_t t384_dualcore_shared;
uint8_t t384_dualcore_frame[T384_CAPTURE_BUFFER_BYTES] __attribute__((aligned(32)));
#if T384_PIPELINE_PACKED_PICTURE && T384_NETWORK_ON_V5F
uint8_t t384_frame_shared_payload[T384_FRAME_SHARED_PAYLOAD_BYTES]
    __attribute__((aligned(32)));
uint8_t t384_frame_shared_metadata[T384_FRAME_SHARED_METADATA_BYTES]
    __attribute__((aligned(32)));
#endif
uint8_t t384_frame1_itcm[T384_FRAME1_ITCM_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_dtcm[T384_FRAME1_DTCM_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_code[T384_FRAME1_CODE_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_data[T384_FRAME1_DATA_BYTES] __attribute__((aligned(32)));
uint8_t t384_picture_expand_scratch[T384_PIPELINE_CHUNK_BYTES]
    __attribute__((aligned(32)));
void t384_dualcore_init(void)
{
    memset(&t384_dualcore_shared, 0, sizeof(t384_dualcore_shared));
}
#endif

enum uart_case {
    UART_NORMAL,
    UART_LOST_MODE1_ACK,
    UART_MODE1_REJECT,
    UART_RESTORE_READBACK_FAIL,
};

static enum uart_case uart_case;
static uint32_t clock_ms;
static uint8_t command[23], reply[32];
static size_t command_length, reply_length, reply_offset;
static uint8_t module_mode, yuv_format;
static unsigned mode1_sets, mode0_sets, mode_queries, yuv_queries;
static unsigned forbidden_dvp_sets, forbidden_persist_sets;
static unsigned dma_bank;

uint32_t t384_millis(void) { return clock_ms++; }
void t384_module_files_task(uint32_t now) { (void)now; }
void RCC_HBPeriphClockCmd(uint32_t p, FunctionalState s) { (void)p; (void)s; }
void USART_ITConfig(USART_TypeDef *u, uint16_t interrupt, FunctionalState state)
{
    (void)u; (void)interrupt; (void)state;
}

static void append_reply(uint8_t status, const uint8_t *data, size_t length)
{
    assert(reply_length + length + 9u <= sizeof(reply));
    const size_t start = reply_length;
    reply[reply_length++] = 0xBEu;
    reply[reply_length++] = 0xAAu;
    reply[reply_length++] = (uint8_t)(length + 1u);
    reply[reply_length++] = 0u;
    reply[reply_length++] = status;
    if (length != 0u) {
        memcpy(reply + reply_length, data, length);
        reply_length += length;
    }
    const uint16_t crc = t384_mini2_crc16_xmodem(reply + start,
                                                   reply_length - start);
    reply[reply_length++] = (uint8_t)crc;
    reply[reply_length++] = (uint8_t)(crc >> 8);
    reply[reply_length++] = 0xEBu;
    reply[reply_length++] = 0xAAu;
}

static void respond(void)
{
    assert(command_length == sizeof(command));
    assert(t384_mini2_crc16_xmodem(command + 5u, 16u) ==
           ((uint16_t)command[21] | ((uint16_t)command[22] << 8)));
    reply_length = reply_offset = 0u;
    const uint8_t index = command[7];
    if (index == 0x46u) ++forbidden_dvp_sets;
    if (index == 0x49u) ++forbidden_persist_sets;
    if (index == 0x45u) {
        const uint8_t requested = command[9];
        assert(requested == 0u || requested == 1u);
        if (requested == 1u) {
            ++mode1_sets;
            if (uart_case == UART_MODE1_REJECT) {
                append_reply(1u, NULL, 0u);
                return;
            }
            module_mode = 1u; /* Lost ACK can still have taken effect. */
            if (uart_case == UART_LOST_MODE1_ACK) return;
        } else {
            ++mode0_sets;
            if (uart_case != UART_RESTORE_READBACK_FAIL) module_mode = 0u;
        }
        append_reply(0u, NULL, 0u);
        return;
    }
    if (index == 0x85u) {
        ++mode_queries;
        append_reply(0u, &module_mode, 1u);
        return;
    }
    if (index == 0x8Cu) {
        ++yuv_queries;
        append_reply(0u, &yuv_format, 1u);
        return;
    }
    assert(!"mode probe must issue only 0x45, 0x85, 0x8c after setup");
}

FlagStatus USART_GetFlagStatus(USART_TypeDef *u, uint16_t flag)
{
    (void)u;
    ++clock_ms;
    return flag == USART_FLAG_RXNE
        ? (reply_offset < reply_length ? SET : RESET) : SET;
}
void USART_SendData(USART_TypeDef *u, uint16_t data)
{
    (void)u;
    assert(command_length < sizeof(command));
    command[command_length++] = (uint8_t)data;
    if (command_length == sizeof(command)) {
        respond();
        command_length = 0u;
    }
}
uint16_t USART_ReceiveData(USART_TypeDef *u)
{
    (void)u;
    assert(reply_offset < reply_length);
    return reply[reply_offset++];
}

static void interrupt(uint8_t flags)
{
    fake_dvp.IFR = flags;
    DVP_IRQHandler();
    fake_dvp.IFR = 0u; /* Host RAM cannot emulate the MMIO RW1Z register. */
}

static void reset_case(enum uart_case next)
{
    uart_case = next;
    clock_ms = 0u;
    command_length = reply_length = reply_offset = 0u;
    module_mode = 0u;
    yuv_format = 2u; /* Native YUYV; production normalizes it to UYVY. */
    mode1_sets = mode0_sets = mode_queries = yuv_queries = 0u;
    forbidden_dvp_sets = forbidden_persist_sets = 0u;
    dma_bank = 0u;
    irq_enabled = false;
    memset(&fake_dvp, 0, sizeof(fake_dvp));
    memset((void *)&source_stats, 0, sizeof(source_stats));
    memset((void *)&mode_probe, 0, sizeof(mode_probe));
    t384_frame_pipeline_init();
    source_stats.initialized = 1u;
    source_stats.stream_ready = 1u;
    source_stats.frame_mode = T384_FRAME_MODE_PICTURE;
    source_stats.pixel_format = T384_FRAME_PIXEL_FORMAT_UYVY;
    source_stats.mini2_query_yuv_valid = 1u;
    source_stats.mini2_query_yuv_format = yuv_format;
    source_stats.mini2_query_stream_mode_valid = 1u;
    source_stats.mini2_query_stream_mode_0x85 = 0u;
    mini2_mode_probe_start();
    assert(mode_probe.state == T384_MODE_PROBE_PENDING);
    assert(mode1_sets == 0u && mode0_sets == 0u && mode_queries == 0u);
}

static void start_observation(void)
{
    t384_frame_source_task();
    assert(mode_probe.state == T384_MODE_PROBE_OBSERVING ||
           mode_probe.state == T384_MODE_PROBE_RESTORING ||
           mode_probe.state == T384_MODE_PROBE_VERIFYING);
}

static void fill_picture_block(uint8_t *data)
{
    for (uint32_t i = 0u; i < T384_MINI2_DMA_BLOCK_BYTES; i += 4u) {
        /* Native baseline format 2 is YUYV: Y may vary; U/V must stay
         * constant across a packed-Picture block. */
        data[i] = (uint8_t)(i >> 4); data[i + 1u] = 80u;
        data[i + 2u] = (uint8_t)(255u - (i >> 4)); data[i + 3u] = 176u;
    }
}

static void physical_picture_frame(bool fifo_fault)
{
    interrupt(RB_DVP_IF_STR_FRM);
    const uint32_t blocks = T384_MINI2_DVP_EXPECTED_ROWS /
                            T384_MINI2_DMA_BLOCK_ROWS;
    for (uint32_t block = 0u; block < blocks; ++block) {
        fill_picture_block(dvp_row_sink[dma_bank]);
        dma_bank ^= 1u;
        fake_dvp.CR1 = RB_DVP_DMA_EN |
            (dma_bank != 0u ? RB_DVP_BUF_TOG : 0u);
        interrupt((uint8_t)(RB_DVP_IF_ROW_DONE |
            (fifo_fault && block == 0u ? RB_DVP_IF_FIFO_OV : 0u) |
            (block + 1u == blocks ? RB_DVP_IF_FRM_DONE | RB_DVP_IF_STP_FRM : 0u)));
    }
}

static void restore_after_observation(void)
{
    clock_ms += T384_640_MODE_PROBE_MS + 1u;
    t384_frame_source_task();
}

int main(void)
{
#if T384_RAW16_PROFILE != 640u
#error "mode probe smoke is deliberately 640-only"
#endif
    reset_case(UART_NORMAL);
    start_observation();
    assert(mode_probe.state == T384_MODE_PROBE_OBSERVING);
    assert(mode1_sets == 1u && mode0_sets == 0u && mode_queries == 1u);
    assert(source_stats.stream_ready == 0u);
    assert(source_stats.frame_mode == T384_FRAME_MODE_UNKNOWN);
    assert(forbidden_dvp_sets == 0u && forbidden_persist_sets == 0u);
    assert(!t384_module_file_port_pause(NULL, NULL));
    /* Candidate mode may be observed, but it cannot lease/publish the HTTP
     * pipeline while stream_ready is deliberately false. */
    physical_picture_frame(false);
    t384_frame_pipeline_stats_t candidate_pipeline;
    t384_frame_pipeline_get_stats(&candidate_pipeline);
    assert(mode_probe.frames == 1u && mode_probe.complete_frames == 1u);
    assert(candidate_pipeline.frames_completed == 0u);
    assert(source_stats.published_frames == 0u);
    restore_after_observation();
    assert(mode_probe.state == T384_MODE_PROBE_VERIFYING);
    assert(mode0_sets == 1u && mode_queries == 2u && yuv_queries == 1u);
    assert(source_stats.stream_ready == 0u);
    assert(forbidden_dvp_sets == 0u && forbidden_persist_sets == 0u);
    physical_picture_frame(false);
    physical_picture_frame(true);  /* Bad frame resets the consecutive count. */
    physical_picture_frame(false);
    physical_picture_frame(false);
    assert(mode_probe.picture_complete_frames == 3u);
    assert(mode_probe.picture_bad_frames == 1u);
    assert(mode_probe.picture_consecutive_frames == 2u);
    t384_frame_source_task();
    assert(mode_probe.state == T384_MODE_PROBE_VERIFYING);
    physical_picture_frame(false);
    t384_frame_source_task();
    assert(mode_probe.state == T384_MODE_PROBE_RESTORED);
    assert(source_stats.stream_ready == 1u);
    assert(forbidden_dvp_sets == 0u && forbidden_persist_sets == 0u);

    reset_case(UART_LOST_MODE1_ACK);
    start_observation();
    assert(mode_probe.state == T384_MODE_PROBE_OBSERVING);
    restore_after_observation();
    assert(mode_probe.state == T384_MODE_PROBE_VERIFYING && mode0_sets == 1u);

    reset_case(UART_MODE1_REJECT);
    start_observation();
    assert(mode_probe.state == T384_MODE_PROBE_VERIFYING);
    assert(mode0_sets == 1u && source_stats.stream_ready == 0u);

    reset_case(UART_RESTORE_READBACK_FAIL);
    start_observation();
    restore_after_observation();
    assert(mode_probe.state == T384_MODE_PROBE_RESTORE_FAILED);
    const unsigned failed_sets = mode0_sets;
    for (unsigned attempt = 0u; attempt < 4u; ++attempt) t384_frame_source_task();
    assert(mode_probe.state == T384_MODE_PROBE_RESTORE_FAILED);
    assert(mode0_sets == failed_sets && source_stats.stream_ready == 0u);
    assert(!t384_module_file_port_pause(NULL, NULL));

    reset_case(UART_NORMAL);
    start_observation();
    /* No DVP activity is also a restoration deadline, never an open stream. */
    restore_after_observation();
    assert(mode_probe.state == T384_MODE_PROBE_VERIFYING);
    clock_ms += T384_640_MODE_PROBE_MS + 1u;
    t384_frame_source_task();
    assert(mode_probe.state == T384_MODE_PROBE_RESTORE_FAILED);
    assert(source_stats.stream_ready == 0u);

    reset_case(UART_NORMAL);
    start_observation();
    /* If the main loop is late, the first ISR after the deadline must only
     * close DMA. It must not send UART from interrupt context or publish. */
    clock_ms = mode_probe_started_ms + T384_640_MODE_PROBE_MS;
    interrupt(RB_DVP_IF_STR_FRM);
    assert(mode_probe.state == T384_MODE_PROBE_RESTORING);
    assert((fake_dvp.CR0 & RB_DVP_ENABLE) == 0u);
    assert(mode0_sets == 0u && source_stats.stream_ready == 0u);
    t384_frame_source_task();
    assert(mode_probe.state == T384_MODE_PROBE_VERIFYING && mode0_sets == 1u);
    puts("640 temporary mode probe: restore/readback/three-picture-frame gate passed");
    return 0;
}
