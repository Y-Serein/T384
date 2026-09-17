/* Exercise the product DVP ISR, real ring, ROI and envelope. Mock MMIO only;
 * unlike the UART fixture this executes actual collection and consumption. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "ch32h417.h"
static DVP_TypeDef fake_dvp;
static bool irq_enabled;
#undef DVP
#define DVP (&fake_dvp)
#define NVIC_DisableIRQ(irq) ((void)(irq), irq_enabled = false)
#define NVIC_EnableIRQ(irq) ((void)(irq), irq_enabled = true)
#include "../firmware/Common/Raw16/t384_frame_source_mini2.c"

#if T384_DUALCORE
#include "t384_dualcore.h"
t384_dualcore_shared_t t384_dualcore_shared;
uint8_t t384_dualcore_frame[T384_RAW16_FRAME_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_itcm[T384_FRAME1_ITCM_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_dtcm[T384_FRAME1_DTCM_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_code[T384_FRAME1_CODE_BYTES] __attribute__((aligned(32)));
uint8_t t384_frame1_data[T384_FRAME1_DATA_BYTES] __attribute__((aligned(32)));
void t384_dualcore_init(void)
{
    memset(&t384_dualcore_shared, 0, sizeof(t384_dualcore_shared));
}
#endif

static uint32_t clock_ms, consumed_offset, consumed_frames;
static unsigned dma_bank;
static bool module_task_event;
static uint32_t uart_tx_bytes;
static bool uart_replies;
static uint8_t uart_command[23], uart_reply[16], uart_digital[3], uart_mode, uart_mode_status;
static size_t uart_command_bytes, uart_reply_bytes, uart_reply_offset;
static void interrupt(uint8_t flags);

uint32_t t384_millis(void) { return clock_ms; }
void t384_module_files_task(uint32_t now)
{
    (void)now;
    if (module_task_event) {
        clock_ms += T384_MINI2_CAPTURE_IDLE_MS;
        interrupt(0u); /* An event during a slow main-task callback. */
    }
}
void RCC_HBPeriphClockCmd(uint32_t periph, FunctionalState state)
{ (void)periph; (void)state; }
FlagStatus USART_GetFlagStatus(USART_TypeDef *usart, uint16_t flag)
{
    (void)usart;
    ++clock_ms;
    return flag == USART_FLAG_RXNE
        ? uart_reply_offset < uart_reply_bytes ? SET : RESET : SET;
}
void USART_SendData(USART_TypeDef *usart, uint16_t data)
{
    (void)usart;
    ++uart_tx_bytes;
    if (!uart_replies) return;
    uart_command[uart_command_bytes++] = (uint8_t)data;
    if (uart_command_bytes != sizeof(uart_command)) return;
    const uint8_t *payload = NULL;
    size_t length = 0u;
    if (uart_command[7] == 0x86u) {
        assert(uart_command[17] == 3u);
        payload = uart_digital; length = sizeof(uart_digital);
    } else if (uart_command[7] == 0x85u) {
        payload = &uart_mode; length = 1u;
    } else {
        assert(uart_command[7] == 0x46u);
        memcpy(uart_digital, uart_command + 9u, sizeof(uart_digital));
    }
    uart_reply[0] = 0xBEu; uart_reply[1] = 0xAAu;
    uart_reply[2] = (uint8_t)(length + 1u); uart_reply[3] = 0u;
    uart_reply[4] = (uart_command[7] == 0x85u) ? uart_mode_status : 0u;
    if (length) memcpy(uart_reply + 5u, payload, length);
    const uint16_t crc = t384_mini2_crc16_xmodem(uart_reply, (uint16_t)(5u + length));
    uart_reply[5u + length] = (uint8_t)crc;
    uart_reply[6u + length] = (uint8_t)(crc >> 8);
    uart_reply[7u + length] = 0xEBu; uart_reply[8u + length] = 0xAAu;
    uart_reply_bytes = 9u + length; uart_reply_offset = 0u;
    uart_command_bytes = 0u;
}
uint16_t USART_ReceiveData(USART_TypeDef *usart)
{ (void)usart; assert(uart_reply_offset < uart_reply_bytes); return uart_reply[uart_reply_offset++]; }

static void reset_capture(void)
{
    t384_frame_pipeline_init();
    memset((void *)&source_stats, 0, sizeof(source_stats));
    memset(&fake_dvp, 0, sizeof(fake_dvp));
    stats_sequence = current_frame_rows = current_chunk_rows = dma_toggle = 0u;
    current_frame_bad = roi_frame_bad = 0u;
    frame_open = frame_published = final_chunk_pending = false;
    active_chunk = NULL;
    first_row_prefix_captured = false;
    file_port_held = false;
    clock_ms = last_dvp_event_ms = fps_started_ms = fps_frames = 0u;
    consumed_offset = consumed_frames = 0u;
    dma_bank = 0u;
    module_task_event = false;
    uart_tx_bytes = 0u;
    uart_replies = false;
    uart_command_bytes = uart_reply_bytes = uart_reply_offset = 0u;
    memset(uart_digital, 0, sizeof(uart_digital));
    uart_mode = T384_MINI2_STREAM_MODE_TPD_Y16;
    uart_mode_status = 0u;
#if T384_RAW16_PROFILE == 384u
    last_module_probe_ms = 0u;
    last_module_rearm_ms = 0u;
    module_rearm_pending = false;
#endif
    source_stats.initialized = source_stats.stream_ready = 1u;
    source_stats.frame_mode = T384_FRAME_MODE_TPD_Y16;
    source_stats.pixel_format = T384_FRAME_PIXEL_FORMAT_Y16_BE;
    mini2_dvp_configure();
    fake_dvp.CR1 = RB_DVP_DMA_EN;
    fake_dvp.CR0 |= RB_DVP_ENABLE;
    irq_enabled = true;
}

static void interrupt(uint8_t flags)
{
    ++clock_ms;
    fake_dvp.IFR = flags;
    DVP_IRQHandler();
    fake_dvp.IFR = 0u; /* MMIO RW1Z is not implemented by host RAM. */
}

static void consume_limit(unsigned limit)
{
    t384_frame_chunk_view_t chunk;
    for (unsigned taken = 0u; taken < limit &&
         t384_frame_pipeline_peek(&chunk); ++taken) {
        assert(chunk.frame_offset == consumed_offset);
        assert((chunk.flags & T384_CHUNK_FLAG_TPD_Y16) != 0u);
        assert(((chunk.flags & T384_CHUNK_FLAG_FRAME_START) != 0u) ==
               (consumed_offset == 0u));
        for (uint32_t i = 0u; i < chunk.length; i += 2u) {
            const uint16_t value = (uint16_t)chunk.data[i] << 8 |
                                   chunk.data[i + 1u];
            assert(value == t384_raw16_word(chunk.frame_sequence,
                       (chunk.frame_offset + i) / 2u));
        }
        uint8_t envelope[T384_RAW16_WIRE_HEADER_BYTES];
        t384_raw16_wire_encode(envelope, &chunk);
        consumed_offset += chunk.length;
        if ((chunk.flags & T384_CHUNK_FLAG_FRAME_END) != 0u) {
            assert(consumed_offset == T384_RAW16_FRAME_BYTES);
            consumed_offset = 0u;
            ++consumed_frames;
        }
        t384_frame_pipeline_release();
    }
}
static void consume(void) { consume_limit(T384_PIPELINE_SLOT_COUNT); }

static void start_frame(void)
{
    if (T384_MINI2_DMA_BLOCK_ROWS == 1u) dma_bank = 0u;
    interrupt(RB_DVP_IF_STR_FRM);
}

static void receive_block(uint32_t index, bool with_end)
{
    const uint32_t sequence = source_stats.frames;
    t384_raw16_fill(sequence, index * T384_MINI2_DMA_BLOCK_BYTES,
                    dvp_row_sink[dma_bank], T384_MINI2_DMA_BLOCK_BYTES);
    dma_bank ^= 1u;
    fake_dvp.CR1 = (uint8_t)(RB_DVP_DMA_EN |
        (dma_bank != 0u ? RB_DVP_BUF_TOG : 0u));
    interrupt((uint8_t)(RB_DVP_IF_ROW_DONE |
        (with_end ? RB_DVP_IF_FRM_DONE | RB_DVP_IF_STP_FRM : 0u)));
}

static void drain_unchecked(void)
{
    t384_frame_chunk_view_t chunk;
    while (t384_frame_pipeline_peek(&chunk)) t384_frame_pipeline_release();
    consumed_offset = 0u;
}

int main(void)
{
    const uint32_t blocks = T384_MINI2_DVP_EXPECTED_ROWS /
                            T384_MINI2_DMA_BLOCK_ROWS;
    reset_capture();
    assert(fake_dvp.COL_NUM == T384_MINI2_DMA_BLOCK_BYTES);
    assert(((fake_dvp.CR0 & RB_DVP_JPEG) != 0u) ==
           (T384_MINI2_DMA_BLOCK_ROWS != 1u));
    for (unsigned frame = 0u; frame < 3u; ++frame) {
        start_frame();
        for (uint32_t block = 0u; block < blocks; ++block) {
            receive_block(block, block + 1u == blocks);
#if T384_DUALCORE
            if (block + 1u < blocks) {
                t384_frame_chunk_view_t incomplete;
                assert(!t384_frame_pipeline_peek(&incomplete));
            }
#endif
            if (T384_MINI2_DMA_BLOCK_ROWS != 1u && block == 0u) {
                interrupt(RB_DVP_IF_FRM_DONE);
                assert(source_stats.capture_active == 1u);
            }
            consume(); /* Main-task runs between DMA block interrupts. */
        }
    }
    assert(consumed_frames == 3u && source_stats.published_frames == 3u);
    assert(source_stats.roi_valid == 1u);
    assert(source_stats.dvp_last_frame_bytes == T384_RAW16_FRAME_BYTES);
    assert(fake_dvp.DMA_BUF0 == (uint32_t)(uintptr_t)dvp_row_sink[0]);
    assert(fake_dvp.DMA_BUF1 == (uint32_t)(uintptr_t)dvp_row_sink[1]);
    puts("DVP real ISR/ring/envelope: complete frames and byte integrity passed");

#if T384_DUALCORE
    /* One old chunk consumed per incoming DMA event: exercise repeated bank
     * reuse while the next frame remains invisible until physical frame end. */
    reset_capture();
    start_frame();
    for (uint32_t block = 0u; block < blocks; ++block)
        receive_block(block, block + 1u == blocks);
    consume_limit(1u);
    for (unsigned frame = 0u; frame < 100u; ++frame) {
        start_frame();
        for (uint32_t block = 0u; block < blocks; ++block) {
            receive_block(block, block + 1u == blocks);
            consume_limit(1u);
        }
    }
    consume();
    assert(consumed_frames == 101u && source_stats.published_frames == 101u);
    assert(source_stats.dropped_frames == 0u && t384_frame_pipeline_empty());
    puts("Dual-bank overlapping DVP/COPY: 101 complete ordered frames, all segments intact");
    reset_capture();
    start_frame();
    for (uint32_t block = 0u; block < blocks; ++block)
        receive_block(block, block + 1u == blocks);
    {
        t384_frame_chunk_view_t old;
        assert(t384_frame_pipeline_peek(&old));
        uint8_t saved[T384_PIPELINE_CHUNK_BYTES];
        memcpy(saved, old.data, sizeof(saved));
        start_frame();
        receive_block(0u, false);
        interrupt(RB_DVP_IF_STP_FRM);
        assert(t384_dualcore_shared.banks[0].state == T384_FRAME_READING);
        assert(t384_dualcore_shared.banks[1].state == T384_FRAME_FREE);
        assert(memcmp(saved, old.data, sizeof(saved)) == 0);
        t384_frame_pipeline_release();
        consumed_offset = old.length;
        consume();
        assert(consumed_frames == 1u && t384_frame_pipeline_empty());
    }
    reset_capture();
    for (unsigned frame = 0u; frame < 2u; ++frame) {
        const uint32_t sequence = frame == 0u ? UINT32_MAX : 0u;
        assert(t384_frame_pipeline_begin_frame(sequence, 0u, T384_CHUNK_FLAG_TPD_Y16));
        for (uint32_t offset = 0u; offset < T384_RAW16_FRAME_BYTES;
             offset += T384_PIPELINE_CHUNK_BYTES) {
            uint8_t *data;
            uint16_t capacity;
            assert(t384_frame_pipeline_acquire_write(&data, &capacity));
            assert(t384_raw16_fill(sequence, offset, data, capacity) == capacity);
            assert(t384_frame_pipeline_commit_write(capacity,
                   offset + capacity == T384_RAW16_FRAME_BYTES));
        }
    }
    {
        t384_frame_chunk_view_t first;
        assert(t384_frame_pipeline_peek(&first) && first.frame_sequence == UINT32_MAX);
        t384_frame_pipeline_release();
        consumed_offset = first.length;
        consume();
        assert(consumed_frames == 2u && t384_frame_pipeline_empty());
    }
    puts("Dual-bank producer abort protects reader; ready ordering survives uint32 wrap");
#endif

    /* No consumer: bounded whole-frame rejection, then next-frame recovery. */
    reset_capture();
    start_frame();
    for (uint32_t block = 0u; block < blocks; ++block)
        receive_block(block, block + 1u == blocks);
    assert(source_stats.published_frames ==
           (T384_DUALCORE || T384_MINI2_DMA_BLOCK_ROWS == 1u ? 1u : 0u));
    t384_frame_pipeline_stats_t stats;
    t384_frame_pipeline_get_stats(&stats);
    assert(stats.acquire_no_slot ==
           (T384_DUALCORE || T384_MINI2_DMA_BLOCK_ROWS == 1u ? 0u : 1u));
    assert(stats.frames_aborted ==
           (T384_DUALCORE || T384_MINI2_DMA_BLOCK_ROWS == 1u ? 0u : 1u));
#if T384_DUALCORE
    /* Keep a COPY consumer lease while three entire physical frames arrive.
     * Producer abort/recovery must never reclaim or overwrite this payload. */
    t384_frame_chunk_view_t held;
    assert(t384_frame_pipeline_peek(&held));
    uint8_t held_copy[T384_PIPELINE_CHUNK_BYTES];
    memcpy(held_copy, held.data, sizeof(held_copy));
    for (unsigned skipped = 0; skipped < 3u; ++skipped) {
        start_frame();
        for (uint32_t block = 0u; block < blocks; ++block)
            receive_block(block, block + 1u == blocks);
        assert(memcmp(held.data, held_copy, sizeof(held_copy)) == 0);
        assert(source_stats.published_frames == 2u);
        assert(source_stats.dropped_frames == skipped);
        assert(source_stats.dvp_bad_frames == 0u);
        t384_frame_pipeline_abort_frame();
        assert(t384_dualcore_shared.banks[0].state == T384_FRAME_READING);
        assert(t384_dualcore_shared.banks[1].state == T384_FRAME_READY);
    }
    t384_frame_pipeline_release();
    consumed_offset = held.length;
    consume(); /* Verify every remaining pixel and FRAME_END of held frame. */
    assert(consumed_frames == 2u && t384_frame_pipeline_empty());
    uint8_t *scratch;
    size_t capacity;
    assert(t384_frame_pipeline_scratch_acquire(&scratch, &capacity));
    assert(capacity == T384_RAW16_FRAME_BYTES);
    assert(!t384_frame_pipeline_begin_frame(123u, 0u, T384_CHUNK_FLAG_TPD_Y16));
    assert(!t384_frame_pipeline_peek(&held));
    t384_frame_pipeline_scratch_release();
    consumed_frames = 0u;
    puts("Dual-core slow consumer: whole-frame drops, held frame intact, scratch exclusion passed");
#endif
    drain_unchecked();
    start_frame();
    for (uint32_t block = 0u; block < blocks; ++block) {
        receive_block(block, block + 1u == blocks);
        consume();
    }
    assert(consumed_frames == 1u);
    puts("DVP no-consumer overflow is bounded; next complete frame recovers");

    /* Short odd-block frame must not shift DMA bank selection next frame. */
    reset_capture();
    start_frame();
    receive_block(0u, false);
    interrupt(RB_DVP_IF_STP_FRM);
    assert(source_stats.published_frames == 0u);
    drain_unchecked();
    start_frame();
    for (uint32_t block = 0u; block < blocks; ++block) {
        receive_block(block, block + 1u == blocks);
        consume();
    }
    assert(consumed_frames == 1u);
    puts("DVP short frame and hardware bank resynchronization passed");

    /* Data beyond expected frame cannot overwrite the leased last block. */
    reset_capture();
    start_frame();
    for (uint32_t block = 0u; block < blocks; ++block) {
        receive_block(block, false);
        consume();
    }
    memset(dvp_row_sink[dma_bank], 0xEE, T384_MINI2_DMA_BLOCK_BYTES);
    dma_bank ^= 1u;
    fake_dvp.CR1 = RB_DVP_DMA_EN | (dma_bank ? RB_DVP_BUF_TOG : 0u);
    interrupt(RB_DVP_IF_ROW_DONE | RB_DVP_IF_STP_FRM);
    assert(source_stats.published_frames == 0u && !frame_open);
    puts("DVP oversized physical frame fails closed before payload write");

    /* FIFO corruption and missing end do not publish a partial frame. */
    reset_capture();
    start_frame();
    receive_block(0u, false);
    interrupt(RB_DVP_IF_FIFO_OV | RB_DVP_IF_STP_FRM);
    assert(source_stats.published_frames == 0u);
    drain_unchecked();
    start_frame();
    receive_block(0u, false);
    clock_ms += T384_MINI2_CAPTURE_IDLE_MS;
    t384_frame_source_task();
    assert(source_stats.dvp_restarts == 1u && !frame_open);
    assert(irq_enabled && (fake_dvp.CR0 & RB_DVP_ENABLE) != 0u);
    assert((fake_dvp.CR1 & RB_DVP_DMA_EN) != 0u);
    assert(source_stats.source_fps_x1000 == 0u);
    drain_unchecked();
    dma_bank = 0u; /* Local receiver reset clears the hardware bank. */
    start_frame();
    for (uint32_t block = 0u; block < blocks; ++block) {
        receive_block(block, block + 1u == blocks);
        consume();
    }
    assert(consumed_frames == 1u);
    puts("DVP FIFO rejection, stopped-DMA recovery and next frame passed");

    module_task_event = true;
    t384_frame_source_task();
    assert(source_stats.dvp_restarts == 1u);
    module_task_event = false;
    last_dvp_event_ms = clock_ms + 1u; /* ISR after the task's time sample. */
    t384_frame_source_task();
    assert(source_stats.dvp_restarts == 1u);
    puts("DVP task/ISR time race does not trigger a false idle reset");

    file_port_held = true;
    clock_ms += T384_MINI2_CAPTURE_IDLE_MS;
    t384_frame_source_task();
    assert(source_stats.dvp_restarts == 1u);
    file_port_held = false;
    source_stats.stream_ready = 0u;
    source_stats.frame_mode = T384_FRAME_MODE_UNKNOWN;
    t384_frame_source_task();
    assert(source_stats.dvp_restarts == 1u);
#if T384_RAW16_PROFILE == 384u
    reset_capture();
    clock_ms = T384_MINI2_MODULE_REARM_MS;
    t384_frame_source_task();
    assert(source_stats.dvp_module_probes == 1u);
    assert(source_stats.dvp_module_rearms == 0u && uart_tx_bytes == 23u);
    assert(source_stats.stream_ready == 0u && !irq_enabled);
    assert((fake_dvp.CR0 & RB_DVP_ENABLE) == 0u);
    clock_ms += T384_MINI2_CAPTURE_IDLE_MS;
    t384_frame_source_task();
    assert(source_stats.dvp_module_probes == 1u && uart_tx_bytes == 23u);
    file_port_held = true;
    clock_ms += T384_MINI2_MODULE_REARM_MS;
    t384_frame_source_task();
    assert(source_stats.dvp_module_probes == 1u && uart_tx_bytes == 23u);
    puts("DVP fault-only UART probe is bounded/rate-limited, fail-closed and table-safe");

    reset_capture();
    uart_replies = true;
    clock_ms = T384_MINI2_MODULE_REARM_MS;
    t384_frame_source_task();
    assert(source_stats.dvp_module_probes == 1u);
    assert(source_stats.dvp_module_rearms == 1u && module_rearm_pending);
    assert(source_stats.stream_ready == 0u && !irq_enabled);
    clock_ms += T384_MINI2_CAPTURE_IDLE_MS;
    t384_frame_source_task();
    assert(source_stats.dvp_module_probes == 2u);
    assert(source_stats.dvp_module_rearms == 1u && !module_rearm_pending);
    assert(source_stats.stream_ready == 1u && irq_enabled);
    assert((fake_dvp.CR0 & RB_DVP_ENABLE) != 0u);
    dma_bank = 0u;
    start_frame();
    for (uint32_t block = 0u; block < blocks; ++block) {
        receive_block(block, block + 1u == blocks);
        consume();
    }
    assert(consumed_frames == 1u && source_stats.published_frames == 1u);
    uart_mode = 3u;
    clock_ms += T384_MINI2_MODULE_REARM_MS;
    t384_frame_source_task();
    assert(source_stats.stream_ready == 0u && !irq_enabled);
    assert(source_stats.dvp_module_rearms == 1u);
    puts("DVP stopped source -> UART rearm -> live confirmation -> real complete frame passed");

    /* A rejected or timed-out 0x85 mode query must not block the 0x46 rearm;
     * only a mode that reads back and disagrees is fail-closed. */
    reset_capture();
    uart_replies = true;
    uart_mode_status = 10u;
    clock_ms = T384_MINI2_MODULE_REARM_MS;
    t384_frame_source_task();
    assert(source_stats.dvp_module_probes == 1u);
    assert(source_stats.mini2_query_digital_valid == 1u);
    assert(source_stats.mini2_query_digital_enabled == 0u);
    assert(source_stats.mini2_query_stream_mode_valid == 0u);
    assert(source_stats.dvp_module_rearms == 1u && module_rearm_pending);
    assert(source_stats.stream_ready == 0u && !irq_enabled);
    clock_ms += T384_MINI2_CAPTURE_IDLE_MS;
    t384_frame_source_task();
    assert(source_stats.dvp_module_probes == 2u);
    assert(source_stats.dvp_module_rearms == 1u && !module_rearm_pending);
    assert(source_stats.stream_ready == 1u && irq_enabled);
    puts("DVP rearm survives 0x85 rejection and confirms via 0x86 read-back");
#else
    assert(uart_tx_bytes == 0u);
#endif
    printf("DVP capture profile=%u, %u bytes/event, %u events/frame passed\n",
           T384_RAW16_PROFILE, T384_MINI2_DMA_BLOCK_BYTES, (unsigned)blocks);
    return 0;
}
