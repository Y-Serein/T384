#include "t384_frame_source.h"

#if !T384_FRAME_SOURCE_SIMULATOR

#include <stddef.h>
#include <string.h>

#include "ch32h417.h"
#include "ch32h417_usart.h"
#include "t384_compiler.h"
#include "t384_frame_pipeline.h"
#include "t384_mini2_protocol.h"
#include "t384_product_config.h"
#include "t384_time.h"

#if T384_MINI2_DVP_ROW_BYTES != (T384_MINI2_DVP_WIDTH * 2u)
#error "MINI2 DVP row bytes must equal width times two"
#endif

#if T384_MINI2_DVP_EXPECTED_ROWS != T384_MINI2_DVP_HEIGHT
#error "MINI2 DVP row count must equal configured height"
#endif

/*
 * The adapter writes complete 8-row blocks into the existing bounded pipeline.
 * A frame is published only when all rows and the frame-end interrupt agree;
 * FIFO/geometry/slot errors discard the whole physical frame.
 */
static uint8_t dvp_row_sink[2][T384_MINI2_DVP_ROW_BYTES]
    __attribute__((aligned(32)));
static volatile t384_frame_source_stats_t source_stats;
static volatile uint32_t stats_sequence;
static volatile uint32_t current_frame_rows;
static volatile uint32_t current_frame_bad;
static volatile uint32_t current_frame_sequence;
static volatile uint32_t current_chunk_rows;
static volatile uint32_t dma_toggle;
static volatile bool frame_open;
static volatile bool frame_published;
static volatile bool final_chunk_pending;
static uint8_t *active_chunk;
static uint32_t fps_started_ms;
static uint32_t fps_frames;

static void mini2_uart0_init(void)
{
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOF, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_USART4, ENABLE);

    GPIO_PinAFConfig(GPIOF, GPIO_PinSource3, GPIO_AF7);
    GPIO_PinAFConfig(GPIOF, GPIO_PinSource4, GPIO_AF7);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    gpio.GPIO_Pin = GPIO_Pin_3;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOF, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_4;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOF, &gpio);

    USART_InitTypeDef usart = {0};
    usart.USART_BaudRate = 115200u;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART4, &usart);
    USART_Cmd(USART4, ENABLE);
}

static void mini2_uart0_flush_rx(void)
{
    while (USART_GetFlagStatus(USART4, USART_FLAG_RXNE) != RESET) {
        (void)USART_ReceiveData(USART4);
    }
}

static int mini2_uart0_send_command(
    const uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES],
    uint8_t *response_data,
    uint16_t *response_data_length,
    uint16_t response_data_capacity)
{
    uint8_t response[T384_MINI2_MAX_RESPONSE_BYTES];
    mini2_uart0_flush_rx();
    ++source_stats.mini2_control_attempts;
    for (size_t i = 0u; i < T384_MINI2_DVP30_COMMAND_BYTES; ++i) {
        while (USART_GetFlagStatus(USART4, USART_FLAG_TXE) == RESET) {
        }
        USART_SendData(USART4, command[i]);
        ++source_stats.mini2_control_tx_bytes;
    }
    while (USART_GetFlagStatus(USART4, USART_FLAG_TC) == RESET) {
    }

    const uint32_t deadline = t384_millis() + 150u;
    size_t received = 0u;
    while ((int32_t)(t384_millis() - deadline) < 0) {
        if (USART_GetFlagStatus(USART4, USART_FLAG_RXNE) == RESET) {
            continue;
        }
        const uint8_t value = (uint8_t)USART_ReceiveData(USART4);
        if (received < sizeof(response)) {
            response[received] = value;
            ++received;
        } else {
            memmove(response, response + 1u, sizeof(response) - 1u);
            response[sizeof(response) - 1u] = value;
        }
        if (received >= T384_MINI2_GENERIC_ACK_BYTES) {
            const uint16_t payload_length =
                (uint16_t)response[2] | ((uint16_t)response[3] << 8);
            const size_t expected_length = (size_t)payload_length + 8u;
            if (payload_length != 0u && expected_length <= received) {
                uint8_t ack_status = 0xFFu;
                const uint8_t *data = NULL;
                uint16_t data_length = 0u;
                if (t384_mini2_parse_response(response, expected_length,
                                               &ack_status, &data,
                                               &data_length)) {
                source_stats.mini2_control_ack_valid = 1u;
                source_stats.mini2_control_ack_status = ack_status;
                if (response_data != NULL && response_data_length != NULL &&
                    data_length <= response_data_capacity) {
                    memcpy(response_data, data, data_length);
                    *response_data_length = data_length;
                }
                return ack_status == 0u ? 1 : 0;
                }
            }
        }
    }
    if (received == 0u) {
        ++source_stats.mini2_control_ack_timeout;
    } else {
        ++source_stats.mini2_control_ack_bad;
    }
    return received == 0u ? 2 : 3;
}

static int mini2_uart0_send_video_command(uint8_t command_index,
                                          uint8_t output_status,
                                          uint8_t format,
                                          uint8_t fps)
{
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES];
    t384_mini2_build_video_command(command, command_index, output_status, format,
                                   fps);
    return mini2_uart0_send_command(command, NULL, NULL, 0u);
}

static int mini2_uart0_query(uint8_t command_index,
                             uint8_t response_data_length,
                             uint8_t *data,
                             uint16_t *data_length)
{
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES];
    t384_mini2_build_query_command(command, command_index,
                                   response_data_length);
    return mini2_uart0_send_command(command, data, data_length,
                                    response_data_length);
}

static int mini2_uart0_query_info(uint8_t subcommand,
                                  uint8_t response_data_length,
                                  uint8_t *data,
                                  uint16_t *data_length)
{
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES];
    t384_mini2_build_info_query_command(command, subcommand,
                                        response_data_length);
    return mini2_uart0_send_command(command, data, data_length,
                                    response_data_length);
}

static void stats_begin(void)
{
    ++stats_sequence;
    T384_MEMORY_BARRIER();
}

static void stats_end(void)
{
    T384_MEMORY_BARRIER();
    ++stats_sequence;
}

static void mini2_dvp_gpio_init(void)
{
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOB | RCC_HB2Periph_GPIOC |
                          RCC_HB2Periph_GPIOD, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;

    /* PCLK=PB12, HSYNC=PB13, VSYNC=PB14. */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource12, GPIO_AF15);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF8);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource14, GPIO_AF15);
    gpio.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14;
    GPIO_Init(GPIOB, &gpio);

    /* DATA0..3=PC6..PC9; FSYNC=PC12 remains a passive observation input. */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource6, GPIO_AF13);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource7, GPIO_AF13);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource8, GPIO_AF13);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource9, GPIO_AF13);
    gpio.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 |
                    GPIO_Pin_12;
    GPIO_Init(GPIOC, &gpio);

    /* DATA4..7=PD12..PD15. */
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource12, GPIO_AF13);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource13, GPIO_AF13);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource14, GPIO_AF13);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource15, GPIO_AF13);
    gpio.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_Init(GPIOD, &gpio);
}

static void mini2_dvp_reset(void)
{
    DVP->CR0 &= (uint8_t)~RB_DVP_ENABLE;
    DVP->CR1 &= (uint8_t)~RB_DVP_DMA_EN;
    NVIC_DisableIRQ(DVP_IRQn);
    DVP->CR1 |= RB_DVP_ALL_CLR | RB_DVP_RCV_CLR;
    DVP->CR1 &= (uint8_t)~(RB_DVP_ALL_CLR | RB_DVP_RCV_CLR);
    DVP->IFR = RB_DVP_IF_STR_FRM | RB_DVP_IF_ROW_DONE |
               RB_DVP_IF_FRM_DONE | RB_DVP_IF_FIFO_OV |
               RB_DVP_IF_STP_FRM;
}

static void mini2_dvp_configure(void)
{
    RCC_HBPeriphClockCmd(RCC_HBPeriph_DVP, ENABLE);
    mini2_dvp_reset();

    uint8_t cr0 = RB_DVP_D8_MOD;
#if T384_MINI2_DVP_PCLK_FALLING
    cr0 |= RB_DVP_P_POLAR;
#endif
#if T384_MINI2_DVP_HSYNC_LOW
    cr0 |= RB_DVP_H_POLAR;
#endif
#if T384_MINI2_DVP_VSYNC_HIGH
    cr0 |= RB_DVP_V_POLAR;
#endif
    DVP->CR0 = cr0;
    DVP->CR1 = 0u;
    DVP->ROW_NUM = T384_MINI2_DVP_EXPECTED_ROWS;
    DVP->COL_NUM = T384_MINI2_DVP_ROW_BYTES;
    DVP->DMA_BUF0 = (uint32_t)(uintptr_t)dvp_row_sink[0];
    DVP->DMA_BUF1 = (uint32_t)(uintptr_t)dvp_row_sink[1];
    DVP->HOFFCNT = 0u;
    DVP->VST = 0u;
    DVP->CAPCNT = 0u;
    DVP->VLINE = 0u;
    DVP->IER = RB_DVP_IE_STR_FRM | RB_DVP_IE_ROW_DONE |
               RB_DVP_IE_FRM_DONE | RB_DVP_IE_FIFO_OV |
               RB_DVP_IE_STP_FRM;
}

static void finish_frame_isr(uint32_t now)
{
    if (final_chunk_pending && frame_open && current_frame_bad == 0u &&
        current_frame_rows == T384_MINI2_DVP_EXPECTED_ROWS) {
        if (t384_frame_pipeline_commit_write(
                (uint16_t)(current_chunk_rows * T384_MINI2_DVP_ROW_BYTES),
                true)) {
            frame_published = true;
            frame_open = false;
            active_chunk = NULL;
        } else {
            current_frame_bad = 1u;
        }
    }
    if (frame_open) {
        t384_frame_pipeline_abort_frame();
        frame_open = false;
        active_chunk = NULL;
        current_frame_bad = 1u;
    }
    ++source_stats.dvp_frame_ends;
    ++source_stats.frames;
    if (frame_published) {
        ++source_stats.published_frames;
    } else {
        ++source_stats.dropped_frames;
    }
    ++fps_frames;
    source_stats.dvp_last_frame_rows = current_frame_rows;
    source_stats.dvp_last_frame_bytes =
        current_frame_rows * T384_MINI2_DVP_ROW_BYTES;
    if (current_frame_bad != 0u ||
        current_frame_rows != T384_MINI2_DVP_EXPECTED_ROWS) {
        ++source_stats.dvp_bad_frames;
    }
    source_stats.capture_active = 0u;
    current_frame_rows = 0u;
    current_frame_bad = 0u;
    current_chunk_rows = 0u;
    frame_open = false;
    frame_published = false;
    final_chunk_pending = false;
    active_chunk = NULL;

    const uint32_t elapsed = (uint32_t)(now - fps_started_ms);
    if (elapsed >= 1000u) {
        source_stats.source_fps_x1000 =
            (uint32_t)(((uint64_t)fps_frames * 1000000u) / elapsed);
        fps_started_ms = now;
        fps_frames = 0u;
    }
}

void DVP_IRQHandler(void) T384_FAST_ISR;

void DVP_IRQHandler(void)
{
    const uint8_t flags = DVP->IFR;
    DVP->IFR = flags;
    const uint32_t now = t384_millis();

    stats_begin();
    if ((flags & RB_DVP_IF_STR_FRM) != 0u) {
        if (frame_open) {
            t384_frame_pipeline_abort_frame();
        }
        ++source_stats.dvp_frame_starts;
        current_frame_rows = 0u;
        current_frame_bad = 0u;
        current_frame_sequence = source_stats.frames;
        current_chunk_rows = 0u;
        dma_toggle = 0u;
        frame_open = false;
        frame_published = false;
        final_chunk_pending = false;
        source_stats.capture_active = 1u;

        uint8_t *destination = NULL;
        uint16_t capacity = 0u;
        if (t384_frame_pipeline_empty() &&
            t384_frame_pipeline_begin_frame(current_frame_sequence, now, 0u) &&
            t384_frame_pipeline_acquire_write(&destination, &capacity) &&
            capacity >= T384_PIPELINE_CHUNK_BYTES) {
            active_chunk = destination;
            frame_open = true;
        } else {
            current_frame_bad = 1u;
            t384_frame_pipeline_abort_frame();
        }
    }

    if ((flags & RB_DVP_IF_FIFO_OV) != 0u) {
        ++source_stats.dvp_fifo_overflows;
        current_frame_bad = 1u;
        if (frame_open) {
            t384_frame_pipeline_abort_frame();
            frame_open = false;
            active_chunk = NULL;
        }
    }

    if ((flags & RB_DVP_IF_ROW_DONE) != 0u) {
        ++source_stats.dvp_row_events;
        if (source_stats.capture_active != 0u) {
            ++current_frame_rows;
            source_stats.dvp_observed_bytes += T384_MINI2_DVP_ROW_BYTES;
            if (current_frame_rows > T384_MINI2_DVP_EXPECTED_ROWS) {
                current_frame_bad = 1u;
            }
            const uint32_t completed_sink = dma_toggle & 1u;
            if (frame_open && active_chunk != NULL) {
                memcpy(active_chunk +
                           current_chunk_rows * T384_MINI2_DVP_ROW_BYTES,
                       dvp_row_sink[completed_sink],
                       T384_MINI2_DVP_ROW_BYTES);
                ++current_chunk_rows;
            }
            if ((dma_toggle & 1u) == 0u) {
                DVP->DMA_BUF0 =
                    (uint32_t)(uintptr_t)dvp_row_sink[completed_sink];
            } else {
                DVP->DMA_BUF1 =
                    (uint32_t)(uintptr_t)dvp_row_sink[completed_sink];
            }
        } else {
            ++source_stats.dvp_orphan_rows;
        }

        const bool frame_end =
            current_frame_rows == T384_MINI2_DVP_EXPECTED_ROWS;
        const bool chunk_end = current_chunk_rows == T384_PIPELINE_CHUNK_ROWS;
        if (source_stats.capture_active != 0u && frame_open && frame_end) {
            if (!chunk_end) {
                current_frame_bad = 1u;
            } else {
                final_chunk_pending = true;
            }
        } else if (source_stats.capture_active != 0u && frame_open &&
                   chunk_end) {
            if (!t384_frame_pipeline_commit_write(
                    (uint16_t)(current_chunk_rows * T384_MINI2_DVP_ROW_BYTES),
                    false)) {
                current_frame_bad = 1u;
                t384_frame_pipeline_abort_frame();
                frame_open = false;
                active_chunk = NULL;
            } else {
                uint8_t *destination = NULL;
                uint16_t capacity = 0u;
                current_chunk_rows = 0u;
                if (!t384_frame_pipeline_acquire_write(&destination,
                                                       &capacity) ||
                    capacity < T384_PIPELINE_CHUNK_BYTES) {
                    current_frame_bad = 1u;
                    t384_frame_pipeline_abort_frame();
                    frame_open = false;
                    active_chunk = NULL;
                } else {
                    active_chunk = destination;
                }
            }
        }

        ++dma_toggle;
    }

    if ((flags & RB_DVP_IF_FRM_DONE) != 0u) {
        ++source_stats.dvp_frame_done_irqs;
    }
    if ((flags & RB_DVP_IF_STP_FRM) != 0u) {
        ++source_stats.dvp_stop_frame_irqs;
    }
    if ((flags & (RB_DVP_IF_FRM_DONE | RB_DVP_IF_STP_FRM)) != 0u &&
        source_stats.capture_active != 0u) {
        finish_frame_isr(now);
    }
    stats_end();
}

bool t384_frame_source_init(void)
{
    memset((void *)&source_stats, 0, sizeof(source_stats));
    memset(dvp_row_sink, 0, sizeof(dvp_row_sink));
    stats_sequence = 0u;
    current_frame_rows = 0u;
    current_frame_bad = 0u;
    current_frame_sequence = 0u;
    current_chunk_rows = 0u;
    dma_toggle = 0u;
    frame_open = false;
    frame_published = false;
    final_chunk_pending = false;
    active_chunk = NULL;
    fps_started_ms = t384_millis();
    fps_frames = 0u;

    source_stats.synthetic = 0u;
    source_stats.target_bps =
        T384_MINI2_DVP_WIDTH * T384_MINI2_DVP_HEIGHT * 2u *
        T384_MINI2_DVP_FPS;
    source_stats.stream_ready = 0u;
    mini2_dvp_gpio_init();
    mini2_uart0_init();
    mini2_dvp_configure();
    source_stats.initialized = 1u;

    uint8_t query_data[32] = {0};
    uint16_t query_length = 0u;
    const int detector_query = mini2_uart0_query(0x84u, 1u, query_data,
                                                 &query_length);
    source_stats.mini2_query_detector_status =
        source_stats.mini2_control_ack_status;
    if (detector_query == 1 && query_length == 1u) {
        source_stats.mini2_query_detector_valid = 1u;
        source_stats.mini2_query_detector_fps = query_data[0];
    }

    memset(query_data, 0, sizeof(query_data));
    query_length = 0u;
    const int digital_query = mini2_uart0_query(0x86u, 4u, query_data,
                                                &query_length);
    source_stats.mini2_query_digital_status =
        source_stats.mini2_control_ack_status;
    if (digital_query == 1 && query_length == 4u) {
        source_stats.mini2_query_digital_valid = 1u;
        source_stats.mini2_query_digital_enabled = query_data[0];
        source_stats.mini2_query_digital_format = query_data[1];
        source_stats.mini2_query_digital_fps = query_data[2];
    }

    memset(query_data, 0, sizeof(query_data));
    query_length = 0u;
    const int name_query = mini2_uart0_query_info(0x01u, 32u, query_data,
                                                  &query_length);
    if (name_query == 1 && query_length != 0u) {
        const size_t copy_length = query_length <
                                       sizeof(source_stats.mini2_device_name) - 1u
                                       ? query_length
                                       : sizeof(source_stats.mini2_device_name) - 1u;
        memcpy((void *)source_stats.mini2_device_name, query_data, copy_length);
        source_stats.mini2_device_name[copy_length] = '\0';
        source_stats.mini2_device_name_valid = 1u;
    }

    memset(query_data, 0, sizeof(query_data));
    query_length = 0u;
    const int version_query = mini2_uart0_query_info(0x02u, 11u, query_data,
                                                     &query_length);
    if (version_query == 1 && query_length != 0u) {
        const size_t copy_length = query_length <
                                       sizeof(source_stats.mini2_firmware_version) - 1u
                                       ? query_length
                                       : sizeof(source_stats.mini2_firmware_version) - 1u;
        memcpy((void *)source_stats.mini2_firmware_version, query_data,
               copy_length);
        source_stats.mini2_firmware_version[copy_length] = '\0';
        source_stats.mini2_firmware_version_valid = 1u;
    }

    /* Factory default has digital output disabled; enable DVP/50Hz without
     * persisting the setting.  The HTTP gate opens only after the module
     * acknowledges this DVP command; frame counters expose any geometry/FIFO
     * errors for board validation.
     */
    source_stats.mini2_control_digital_off_status =
        (uint32_t)mini2_uart0_send_video_command(0x46u, 0x00u, 0x00u, 0x00u);
    source_stats.mini2_control_analog_off_status =
        (uint32_t)mini2_uart0_send_video_command(0x4Au, 0x00u, 0x00u, 0x00u);
    source_stats.mini2_control_detector30_status =
        (uint32_t)mini2_uart0_send_video_command(0x44u, T384_MINI2_DVP_FPS,
                                                 0x00u, 0x00u);
    source_stats.mini2_control_dvp30_status =
        (uint32_t)mini2_uart0_send_video_command(0x46u, 0x01u, 0x01u,
                                                 T384_MINI2_DVP_FPS);
    source_stats.stream_ready =
        source_stats.mini2_control_dvp30_status == 1u ? 1u : 0u;

    NVIC_SetPriority(DVP_IRQn, 0u);
    NVIC_EnableIRQ(DVP_IRQn);
    DVP->CR1 |= RB_DVP_DMA_EN;
    DVP->CR0 |= RB_DVP_ENABLE;
    return true;
}

void t384_frame_source_task(void)
{
    /* Capture and bounded pipeline publication are interrupt-driven. */
}

const char *t384_frame_source_name(void)
{
    return "mini2-dvp-raw16-v1";
}

bool t384_frame_source_stream_ready(void)
{
    return source_stats.stream_ready != 0u;
}

void t384_frame_source_get_stats(t384_frame_source_stats_t *out)
{
    if (out == NULL) {
        return;
    }

    uint32_t before;
    uint32_t after;
    do {
        before = stats_sequence;
        if ((before & 1u) != 0u) {
            continue;
        }
        T384_MEMORY_BARRIER();
        out->initialized = source_stats.initialized;
        out->synthetic = source_stats.synthetic;
        out->target_bps = source_stats.target_bps;
        out->frames = source_stats.frames;
        out->published_frames = source_stats.published_frames;
        out->dropped_frames = source_stats.dropped_frames;
        out->schedule_overruns = source_stats.schedule_overruns;
        out->source_fps_x1000 = source_stats.source_fps_x1000;
        out->stream_ready = source_stats.stream_ready;
        out->capture_active = source_stats.capture_active;
        out->dvp_frame_starts = source_stats.dvp_frame_starts;
        out->dvp_row_events = source_stats.dvp_row_events;
        out->dvp_frame_done_irqs = source_stats.dvp_frame_done_irqs;
        out->dvp_stop_frame_irqs = source_stats.dvp_stop_frame_irqs;
        out->dvp_frame_ends = source_stats.dvp_frame_ends;
        out->dvp_fifo_overflows = source_stats.dvp_fifo_overflows;
        out->dvp_orphan_rows = source_stats.dvp_orphan_rows;
        out->dvp_bad_frames = source_stats.dvp_bad_frames;
        out->dvp_last_frame_rows = source_stats.dvp_last_frame_rows;
        out->dvp_last_frame_bytes = source_stats.dvp_last_frame_bytes;
        out->dvp_observed_bytes = source_stats.dvp_observed_bytes;
        out->mini2_control_attempts = source_stats.mini2_control_attempts;
        out->mini2_control_tx_bytes = source_stats.mini2_control_tx_bytes;
        out->mini2_control_ack_valid = source_stats.mini2_control_ack_valid;
        out->mini2_control_ack_status = source_stats.mini2_control_ack_status;
        out->mini2_control_ack_timeout = source_stats.mini2_control_ack_timeout;
        out->mini2_control_ack_bad = source_stats.mini2_control_ack_bad;
        out->mini2_control_digital_off_status =
            source_stats.mini2_control_digital_off_status;
        out->mini2_control_analog_off_status =
            source_stats.mini2_control_analog_off_status;
        out->mini2_control_detector30_status =
            source_stats.mini2_control_detector30_status;
        out->mini2_control_dvp30_status = source_stats.mini2_control_dvp30_status;
        out->mini2_query_detector_valid = source_stats.mini2_query_detector_valid;
        out->mini2_query_detector_status = source_stats.mini2_query_detector_status;
        out->mini2_query_detector_fps = source_stats.mini2_query_detector_fps;
        out->mini2_query_digital_valid = source_stats.mini2_query_digital_valid;
        out->mini2_query_digital_status = source_stats.mini2_query_digital_status;
        out->mini2_query_digital_enabled = source_stats.mini2_query_digital_enabled;
        out->mini2_query_digital_format = source_stats.mini2_query_digital_format;
        out->mini2_query_digital_fps = source_stats.mini2_query_digital_fps;
        memcpy(out->mini2_device_name, (const void *)source_stats.mini2_device_name,
               sizeof(out->mini2_device_name));
        memcpy(out->mini2_firmware_version,
               (const void *)source_stats.mini2_firmware_version,
               sizeof(out->mini2_firmware_version));
        out->mini2_device_name_valid = source_stats.mini2_device_name_valid;
        out->mini2_firmware_version_valid =
            source_stats.mini2_firmware_version_valid;
        T384_MEMORY_BARRIER();
        after = stats_sequence;
    } while (before != after || (after & 1u) != 0u);
}

#endif
