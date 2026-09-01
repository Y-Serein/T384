/*
 * OV2640 validation capture for the T384 NCM control-plane firmware.
 *
 * The GPIO/SCCB and register-table portions are derived from the WCH
 * Petros_DVP V1.2 example (2025-07-28).  WCH's source/binary restriction for
 * WCH microcontrollers remains applicable.  This is an OV2640 validation
 * path only; it is not the MINI2/RAW16 product data path.
 */
#include "t384_camera.h"

#include <stddef.h>

#include "ch32h417.h"
#include "ch32h417_dvp.h"
#include "debug.h"
#include "t384_compiler.h"
#include "t384_time.h"
#include "ov2640_jpeg_regs.h"

#define OV2640_SCCB_ID 0x60u
#define OV2640_MID 0x7FA2u
#define OV2640_PID 0x2642u

#define T384_CAMERA_WIDTH 352u
#define T384_CAMERA_HEIGHT 288u
#define T384_CAMERA_DMA_CHUNK 512u
#define T384_CAMERA_SLOT_COUNT 2u
#define T384_CAMERA_SLOT_SIZE (64u * 1024u)
#define T384_CAMERA_TIMEOUT_MS 1000u

static uint8_t frame_slots[T384_CAMERA_SLOT_COUNT][T384_CAMERA_SLOT_SIZE]
    __attribute__((aligned(32)));
static uint8_t overflow_sink[T384_CAMERA_DMA_CHUNK] __attribute__((aligned(32)));

static volatile bool camera_initialized;
static volatile bool capture_active;
static volatile bool capture_frame_started;
static volatile bool capture_overflow;
static volatile bool complete_overflow;
static volatile bool frame_complete_pending;
static bool no_slot_reported;
static volatile uint32_t capture_offset;
static volatile uint32_t capture_span;
static volatile uint32_t dma_count;
static volatile uint32_t capture_started_ms;
static volatile int8_t capture_slot = -1;
static volatile int8_t complete_slot = -1;
static volatile int8_t processing_slot = -1;
static volatile int8_t ready_slot = -1;
static volatile int8_t leased_slot = -1;
static uint32_t slot_frame_start[T384_CAMERA_SLOT_COUNT];
static uint32_t slot_frame_length[T384_CAMERA_SLOT_COUNT];
static uint32_t fps_window_started_ms;
static uint32_t fps_window_frame_ends;
static volatile t384_camera_stats_t camera_stats;

#define SCCB_SCL_OUT() \
    do { GPIOB->CFGHR = (GPIOB->CFGHR & 0xFFFFF0FFu) | ((uint32_t)3u << 8); } while (0)
#define SCCB_SDA_IN() \
    do { GPIOB->CFGHR = (GPIOB->CFGHR & 0xFFFF0FFFu) | ((uint32_t)8u << 12); } while (0)
#define SCCB_SDA_OUT() \
    do { GPIOB->CFGHR = (GPIOB->CFGHR & 0xFFFF0FFFu) | ((uint32_t)3u << 12); } while (0)
#define SCCB_SCL_SET() do { GPIOB->BSHR = GPIO_Pin_10; } while (0)
#define SCCB_SCL_CLR() do { GPIOB->BCR = GPIO_Pin_10; } while (0)
#define SCCB_SDA_SET() do { GPIOB->BSHR = GPIO_Pin_11; } while (0)
#define SCCB_SDA_CLR() do { GPIOB->BCR = GPIO_Pin_11; } while (0)
#define SCCB_SDA_HIGH() ((GPIOB->INDR & GPIO_Pin_11) != 0u)

#define OV_RESET_SET() do { GPIOD->BSHR = GPIO_Pin_1; } while (0)
#define OV_RESET_CLR() do { GPIOD->BCR = GPIO_Pin_1; } while (0)
#define OV_PWDN_SET() do { GPIOD->BSHR = GPIO_Pin_0; } while (0)
#define OV_PWDN_CLR() do { GPIOD->BCR = GPIO_Pin_0; } while (0)

static void camera_gpio_init(void)
{
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOB | RCC_HB2Periph_GPIOC |
                           RCC_HB2Periph_GPIOD, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Speed = GPIO_Speed_Very_High;

    /* Petros_DVP official-board mapping: PWDN=PD0, RESET=PD1. */
    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOD, &gpio);

    /* SCCB: SCL=PB10, SDA=PB11. */
    gpio.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);

    /* DVP clock/synchronization: CLK=PB12, HREF=PB13, VSYNC=PB14. */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource12, GPIO_AF15);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF8);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource14, GPIO_AF15);
    gpio.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);

    /* DVP DATA0..3=PC6..PC9, DATA4..7=PD12..PD15. */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource6, GPIO_AF13);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource7, GPIO_AF13);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource8, GPIO_AF13);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource9, GPIO_AF13);
    gpio.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOC, &gpio);

    GPIO_PinAFConfig(GPIOD, GPIO_PinSource12, GPIO_AF13);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource13, GPIO_AF13);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource14, GPIO_AF13);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource15, GPIO_AF13);
    gpio.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_Init(GPIOD, &gpio);

    OV_PWDN_SET();
    OV_RESET_SET();
}

static void sccb_init(void)
{
    SCCB_SCL_OUT();
    SCCB_SDA_OUT();
    SCCB_SCL_SET();
    SCCB_SDA_SET();
}

static void sccb_start(void)
{
    SCCB_SDA_SET();
    SCCB_SCL_SET();
    Delay_Us(50u);
    SCCB_SDA_CLR();
    Delay_Us(50u);
    SCCB_SCL_CLR();
}

static void sccb_stop(void)
{
    SCCB_SDA_CLR();
    Delay_Us(50u);
    SCCB_SCL_SET();
    Delay_Us(50u);
    SCCB_SDA_SET();
    Delay_Us(50u);
}

static bool sccb_write_byte(uint8_t data)
{
    for (unsigned bit = 0u; bit < 8u; ++bit) {
        if ((data & 0x80u) != 0u) {
            SCCB_SDA_SET();
        } else {
            SCCB_SDA_CLR();
        }
        data <<= 1;
        Delay_Us(50u);
        SCCB_SCL_SET();
        Delay_Us(50u);
        SCCB_SCL_CLR();
    }

    SCCB_SDA_IN();
    Delay_Us(50u);
    SCCB_SCL_SET();
    Delay_Us(50u);
    const bool nack = SCCB_SDA_HIGH();
    SCCB_SCL_CLR();
    SCCB_SDA_OUT();
    return !nack;
}

static uint8_t sccb_read_byte(void)
{
    uint8_t data = 0u;
    SCCB_SDA_IN();
    for (unsigned bit = 0u; bit < 8u; ++bit) {
        Delay_Us(50u);
        SCCB_SCL_SET();
        data = (uint8_t)(data << 1);
        if (SCCB_SDA_HIGH()) {
            data = (uint8_t)(data | 1u);
        }
        Delay_Us(50u);
        SCCB_SCL_CLR();
    }
    SCCB_SDA_OUT();
    return data;
}

static void sccb_no_ack(void)
{
    SCCB_SDA_SET();
    SCCB_SCL_SET();
    Delay_Us(50u);
    SCCB_SCL_CLR();
    SCCB_SDA_CLR();
}

static bool sccb_write_reg(uint8_t reg, uint8_t value)
{
    bool ok = true;
    sccb_start();
    ok = sccb_write_byte(OV2640_SCCB_ID) && ok;
    Delay_Us(100u);
    ok = sccb_write_byte(reg) && ok;
    Delay_Us(100u);
    ok = sccb_write_byte(value) && ok;
    sccb_stop();
    return ok;
}

static uint8_t sccb_read_reg(uint8_t reg)
{
    sccb_start();
    (void)sccb_write_byte(OV2640_SCCB_ID);
    Delay_Us(100u);
    (void)sccb_write_byte(reg);
    Delay_Us(100u);
    sccb_stop();
    Delay_Us(100u);

    sccb_start();
    (void)sccb_write_byte((uint8_t)(OV2640_SCCB_ID | 1u));
    Delay_Us(100u);
    const uint8_t value = sccb_read_byte();
    sccb_no_ack();
    sccb_stop();
    return value;
}

static bool ov2640_write_table(const uint8_t table[][2], size_t count)
{
    for (size_t i = 0u; i < count; ++i) {
        if (!sccb_write_reg(table[i][0], table[i][1])) {
            return false;
        }
    }
    return true;
}

static bool ov2640_out_size_set(uint16_t width, uint16_t height)
{
    if ((width % 4u) != 0u || (height % 4u) != 0u) {
        return false;
    }

    const uint16_t out_width = (uint16_t)(width / 4u);
    const uint16_t out_height = (uint16_t)(height / 4u);
    bool ok = true;
    ok = sccb_write_reg(0xFFu, 0x00u) && ok;
    ok = sccb_write_reg(0xE0u, 0x04u) && ok;
    ok = sccb_write_reg(0x5Au, (uint8_t)out_width) && ok;
    ok = sccb_write_reg(0x5Bu, (uint8_t)out_height) && ok;
    ok = sccb_write_reg(0x5Cu, (uint8_t)(((out_width >> 8) & 0x03u) |
                                         ((out_height >> 6) & 0x04u))) && ok;
    ok = sccb_write_reg(0xE0u, 0x00u) && ok;
    return ok;
}

static bool ov2640_init_registers(void)
{
    OV_PWDN_CLR();
    Delay_Ms(10u);
    OV_RESET_CLR();
    Delay_Ms(10u);
    OV_RESET_SET();
    Delay_Ms(10u);
    sccb_init();

    if (!sccb_write_reg(0xFFu, 0x01u) || !sccb_write_reg(0x12u, 0x80u)) {
        return false;
    }
    Delay_Ms(50u);

    const uint16_t mid = (uint16_t)(((uint16_t)sccb_read_reg(0x1Cu) << 8) |
                                    sccb_read_reg(0x1Du));
    const uint16_t pid = (uint16_t)(((uint16_t)sccb_read_reg(0x0Au) << 8) |
                                    sccb_read_reg(0x0Bu));
    camera_stats.sensor_mid = mid;
    camera_stats.sensor_pid = pid;
    if (mid != OV2640_MID || pid != OV2640_PID) {
        return false;
    }

    /* WCH's SVGA table is documented for 30 fps; scale it to fixed 352x288. */
    if (!sccb_write_reg(0xFFu, 0x01u) || !sccb_write_reg(0x12u, 0x80u)) {
        return false;
    }
    Delay_Ms(5u);
    if (!ov2640_write_table(ov2640_svga_init_reg_tbl,
                            sizeof(ov2640_svga_init_reg_tbl) /
                                sizeof(ov2640_svga_init_reg_tbl[0])) ||
        !ov2640_write_table(ov2640_jpeg_reg_tbl,
                            sizeof(ov2640_jpeg_reg_tbl) /
                                sizeof(ov2640_jpeg_reg_tbl[0])) ||
        !ov2640_out_size_set(T384_CAMERA_WIDTH, T384_CAMERA_HEIGHT)) {
        return false;
    }
    return true;
}

static void dvp_reset(void)
{
    DVP->CR0 &= (uint8_t)~RB_DVP_ENABLE;
    DVP->CR1 &= (uint8_t)~RB_DVP_DMA_EN;
    NVIC_DisableIRQ(DVP_IRQn);
    DVP->CR1 |= RB_DVP_ALL_CLR | RB_DVP_RCV_CLR;
    DVP->CR1 &= (uint8_t)~(RB_DVP_ALL_CLR | RB_DVP_RCV_CLR);
    DVP->IFR = RB_DVP_IF_STR_FRM | RB_DVP_IF_ROW_DONE | RB_DVP_IF_FRM_DONE |
               RB_DVP_IF_FIFO_OV | RB_DVP_IF_STP_FRM;
}

static void dvp_configure(void)
{
    RCC_HBPeriphClockCmd(RCC_HBPeriph_DVP, ENABLE);
    dvp_reset();
    DVP->CR0 = RB_DVP_JPEG | RB_DVP_D8_MOD | RB_DVP_V_POLAR;
    /* Match the proven Petros_DVP continuous mode; the ISR freezes frame one. */
    DVP->CR1 = 0u;
    DVP->ROW_NUM = 0u;
    DVP->COL_NUM = T384_CAMERA_DMA_CHUNK;
    DVP->HOFFCNT = 0u;
    DVP->VST = 0u;
    DVP->CAPCNT = 0u;
    DVP->VLINE = 0u;
    DVP->IER = RB_DVP_IE_STR_FRM | RB_DVP_IE_ROW_DONE | RB_DVP_IE_FRM_DONE |
               RB_DVP_IE_FIFO_OV | RB_DVP_IE_STP_FRM;
}

static int8_t find_free_slot(void)
{
    for (int8_t slot = 0; slot < (int8_t)T384_CAMERA_SLOT_COUNT; ++slot) {
        if (slot != capture_slot && slot != complete_slot &&
            slot != processing_slot && slot != ready_slot &&
            slot != leased_slot) {
            return slot;
        }
    }
    return -1;
}

static void dvp_start_capture(int8_t slot)
{
    uint8_t *buffer = frame_slots[(unsigned)slot];
    dvp_reset();
    DVP->CR0 = RB_DVP_JPEG | RB_DVP_D8_MOD | RB_DVP_V_POLAR;
    DVP->CR1 = 0u;
    DVP->ROW_NUM = 0u;
    DVP->COL_NUM = T384_CAMERA_DMA_CHUNK;
    DVP->DMA_BUF0 = (uint32_t)(uintptr_t)buffer;
    DVP->DMA_BUF1 = (uint32_t)(uintptr_t)(buffer + T384_CAMERA_DMA_CHUNK);
    DVP->IER = RB_DVP_IE_STR_FRM | RB_DVP_IE_ROW_DONE | RB_DVP_IE_FRM_DONE |
               RB_DVP_IE_FIFO_OV | RB_DVP_IE_STP_FRM;

    capture_slot = slot;
    capture_frame_started = false;
    capture_offset = 0u;
    capture_span = 0u;
    capture_overflow = false;
    frame_complete_pending = false;
    dma_count = 0u;
    capture_started_ms = t384_millis();
    capture_active = true;
    no_slot_reported = false;
    ++camera_stats.starts;
    DVP->CR1 |= RB_DVP_DMA_EN;
    DVP->CR0 |= RB_DVP_ENABLE;
    NVIC_EnableIRQ(DVP_IRQn);
}

static void restart_capture_if_possible(void)
{
    if (!camera_initialized || capture_active || frame_complete_pending ||
        complete_slot >= 0) {
        return;
    }
    const int8_t slot = find_free_slot();
    if (slot < 0) {
        if (!no_slot_reported) {
            ++camera_stats.dropped_no_slot;
            no_slot_reported = true;
        }
        return;
    }
    dvp_start_capture(slot);
}

static bool find_jpeg(const uint8_t *buffer, uint32_t limit,
                      uint32_t *start, uint32_t *length)
{
    uint32_t soi = 0u;
    bool found_soi = false;
    for (uint32_t i = 0u; i + 1u < limit; ++i) {
        if (!found_soi) {
            if (buffer[i] == 0xFFu && buffer[i + 1u] == 0xD8u) {
                soi = i;
                found_soi = true;
                ++i;
            }
        } else if (buffer[i] == 0xFFu && buffer[i + 1u] == 0xD9u) {
            *start = soi;
            *length = i + 2u - soi;
            return true;
        }
    }
    return false;
}

static void finish_capture_isr(void)
{
    capture_frame_started = false;
    if (frame_complete_pending) {
        DVP->CR0 &= (uint8_t)~RB_DVP_ENABLE;
        DVP->CR1 &= (uint8_t)~RB_DVP_DMA_EN;
        capture_active = false;
        capture_slot = -1;
        ++camera_stats.dropped_no_slot;
        return;
    }

    DVP->CR0 &= (uint8_t)~RB_DVP_ENABLE;
    DVP->CR1 &= (uint8_t)~RB_DVP_DMA_EN;
    uint32_t span = capture_offset;
    if (span > T384_CAMERA_SLOT_SIZE) {
        span = T384_CAMERA_SLOT_SIZE;
    }
    capture_span = span;
    complete_overflow = capture_overflow;
    complete_slot = capture_slot;
    frame_complete_pending = true;

    int8_t next_slot = capture_slot == 0 ? 1 : 0;
    if (next_slot == ready_slot && next_slot != leased_slot) {
        ready_slot = -1;
        ++camera_stats.dropped_ready;
    }
    if (next_slot == leased_slot || next_slot == processing_slot ||
        next_slot == complete_slot) {
        capture_active = false;
        capture_slot = -1;
        return;
    }

    uint8_t *next_buffer = frame_slots[(unsigned)next_slot];
    DVP->DMA_BUF0 = (uint32_t)(uintptr_t)next_buffer;
    DVP->DMA_BUF1 =
        (uint32_t)(uintptr_t)(next_buffer + T384_CAMERA_DMA_CHUNK);
    capture_slot = next_slot;
    capture_frame_started = false;
    capture_offset = 0u;
    capture_overflow = false;
    dma_count = 0u;
    capture_started_ms = t384_millis();
    capture_active = true;
    ++camera_stats.starts;
    DVP->CR1 |= RB_DVP_DMA_EN;
    DVP->CR0 |= RB_DVP_ENABLE;
}

void DVP_IRQHandler(void) T384_FAST_ISR;

void DVP_IRQHandler(void)
{
    const uint8_t flags = DVP->IFR;
    DVP->IFR = flags;

    if ((flags & RB_DVP_IF_STR_FRM) != 0u && capture_active) {
        ++camera_stats.frame_starts;
        capture_frame_started = true;
        capture_offset = 0u;
        dma_count = 0u;
    }

    if ((flags & RB_DVP_IF_ROW_DONE) != 0u && capture_active &&
        capture_frame_started) {
        ++camera_stats.row_chunks;
        const int8_t slot = capture_slot;
        const uint32_t offset = capture_offset;
        const uint32_t next = offset + T384_CAMERA_DMA_CHUNK;
        if (next > T384_CAMERA_SLOT_SIZE || slot < 0) {
            capture_overflow = true;
        } else {
            capture_offset = next;
        }

        const uint32_t next_dma = next + T384_CAMERA_DMA_CHUNK;
        uint8_t *next_buffer = overflow_sink;
        if (slot >= 0 && next_dma <= T384_CAMERA_SLOT_SIZE - T384_CAMERA_DMA_CHUNK) {
            next_buffer = frame_slots[(unsigned)slot] + next_dma;
        }
        if ((dma_count & 1u) == 0u) {
            DVP->DMA_BUF0 = (uint32_t)(uintptr_t)next_buffer;
        } else {
            DVP->DMA_BUF1 = (uint32_t)(uintptr_t)next_buffer;
        }
        ++dma_count;
    }

    if ((flags & RB_DVP_IF_FIFO_OV) != 0u) {
        ++camera_stats.fifo_overflows;
        capture_overflow = true;
    }
    if ((flags & RB_DVP_IF_FRM_DONE) != 0u) {
        ++camera_stats.frame_done_irqs;
    }
    if ((flags & RB_DVP_IF_STP_FRM) != 0u) {
        ++camera_stats.stop_frame_irqs;
    }
    if ((flags & (RB_DVP_IF_FRM_DONE | RB_DVP_IF_STP_FRM)) != 0u &&
        capture_active && capture_frame_started) {
        ++camera_stats.frame_ends;
        finish_capture_isr();
    }
}

bool t384_camera_init(void)
{
    ++camera_stats.init_attempts;
    camera_gpio_init();
    if (!ov2640_init_registers()) {
        ++camera_stats.init_fail;
        camera_initialized = false;
        dvp_configure();
        return false;
    }
    dvp_configure();
    camera_initialized = true;
    ++camera_stats.init_ok;
    fps_window_started_ms = t384_millis();
    fps_window_frame_ends = camera_stats.frame_ends;
    restart_capture_if_possible();
    return true;
}

bool t384_camera_is_initialized(void)
{
    return camera_initialized;
}

void t384_camera_task(void)
{
    const uint32_t now = t384_millis();
    if (capture_active &&
        (uint32_t)(now - capture_started_ms) >= T384_CAMERA_TIMEOUT_MS) {
        dvp_reset();
        capture_active = false;
        capture_frame_started = false;
        capture_slot = -1;
        frame_complete_pending = false;
        complete_slot = -1;
        ++camera_stats.timeouts;
        ++camera_stats.bad_frames;
    }

    uint32_t limit = 0u;
    bool overflow = false;
    int8_t completed = -1;
    bool have_completed = false;
    const bool irq_was_enabled = NVIC_GetStatusIRQ(DVP_IRQn) != 0u;
    NVIC_DisableIRQ(DVP_IRQn);
    if (frame_complete_pending && processing_slot < 0) {
        limit = capture_span;
        overflow = complete_overflow;
        completed = complete_slot;
        processing_slot = completed;
        frame_complete_pending = false;
        complete_slot = -1;
        have_completed = true;
    }
    if (irq_was_enabled) {
        NVIC_EnableIRQ(DVP_IRQn);
    }

    if (have_completed) {
        bool valid = false;
        uint32_t start = 0u;
        uint32_t length = 0u;

        if (completed < 0 || overflow || limit == 0u ||
            limit > T384_CAMERA_SLOT_SIZE) {
            if (overflow) {
                ++camera_stats.overflows;
            }
            ++camera_stats.bad_frames;
        } else if (!find_jpeg(frame_slots[(unsigned)completed], limit,
                              &start, &length)) {
            ++camera_stats.bad_frames;
        } else {
            valid = true;
        }

        const bool publish_irq_was_enabled =
            NVIC_GetStatusIRQ(DVP_IRQn) != 0u;
        NVIC_DisableIRQ(DVP_IRQn);
        if (valid) {
            if (ready_slot >= 0) {
                ++camera_stats.dropped_ready;
            }
            slot_frame_start[(unsigned)completed] = start;
            slot_frame_length[(unsigned)completed] = length;
            ready_slot = completed;
            ++camera_stats.frames;
            ++camera_stats.published_frames;
            camera_stats.bytes += length;
            camera_stats.last_frame_bytes = length;
            if (length > camera_stats.max_frame_bytes) {
                camera_stats.max_frame_bytes = length;
            }
        }
        processing_slot = -1;
        if (publish_irq_was_enabled) {
            NVIC_EnableIRQ(DVP_IRQn);
        }
    }

    const uint32_t elapsed = (uint32_t)(now - fps_window_started_ms);
    if (elapsed >= 1000u) {
        const uint32_t frame_ends = camera_stats.frame_ends;
        camera_stats.source_fps_x1000 =
            elapsed == 0u
                ? 0u
                : ((frame_ends - fps_window_frame_ends) * 1000000u) / elapsed;
        fps_window_frame_ends = frame_ends;
        fps_window_started_ms = now;
    }

    restart_capture_if_possible();
}

bool t384_camera_trigger(void)
{
    ++camera_stats.requests;
    if (!camera_initialized) {
        return false;
    }
    restart_capture_if_possible();
    return true;
}

bool t384_camera_acquire_frame(const uint8_t **data, uint32_t *length)
{
    if (data == NULL || length == NULL) {
        return false;
    }

    bool acquired = false;
    const bool irq_was_enabled = NVIC_GetStatusIRQ(DVP_IRQn) != 0u;
    NVIC_DisableIRQ(DVP_IRQn);
    if (ready_slot >= 0 && leased_slot < 0) {
        const int8_t slot = ready_slot;
        ready_slot = -1;
        leased_slot = slot;
        *data = frame_slots[(unsigned)slot] + slot_frame_start[(unsigned)slot];
        *length = slot_frame_length[(unsigned)slot];
        acquired = true;
    }
    if (irq_was_enabled) {
        NVIC_EnableIRQ(DVP_IRQn);
    }
    return acquired;
}

void t384_camera_release_frame(void)
{
    const bool irq_was_enabled = NVIC_GetStatusIRQ(DVP_IRQn) != 0u;
    NVIC_DisableIRQ(DVP_IRQn);
    if (leased_slot >= 0) {
        slot_frame_start[(unsigned)leased_slot] = 0u;
        slot_frame_length[(unsigned)leased_slot] = 0u;
        leased_slot = -1;
    }
    if (irq_was_enabled) {
        NVIC_EnableIRQ(DVP_IRQn);
    }
}

void t384_camera_get_stats(t384_camera_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->init_attempts = camera_stats.init_attempts;
    out->init_ok = camera_stats.init_ok;
    out->init_fail = camera_stats.init_fail;
    out->sensor_mid = camera_stats.sensor_mid;
    out->sensor_pid = camera_stats.sensor_pid;
    out->requests = camera_stats.requests;
    out->starts = camera_stats.starts;
    out->frame_starts = camera_stats.frame_starts;
    out->row_chunks = camera_stats.row_chunks;
    out->frame_done_irqs = camera_stats.frame_done_irqs;
    out->stop_frame_irqs = camera_stats.stop_frame_irqs;
    out->frame_ends = camera_stats.frame_ends;
    out->fifo_overflows = camera_stats.fifo_overflows;
    out->frames = camera_stats.frames;
    out->published_frames = camera_stats.published_frames;
    out->bad_frames = camera_stats.bad_frames;
    out->overflows = camera_stats.overflows;
    out->timeouts = camera_stats.timeouts;
    out->dropped_ready = camera_stats.dropped_ready;
    out->dropped_no_slot = camera_stats.dropped_no_slot;
    out->bytes = camera_stats.bytes;
    out->last_frame_bytes = camera_stats.last_frame_bytes;
    out->max_frame_bytes = camera_stats.max_frame_bytes;
    out->source_fps_x1000 = camera_stats.source_fps_x1000;
    out->active = capture_active ? 1u : 0u;
    out->ready = ready_slot >= 0 ? 1u : 0u;
    out->leased = leased_slot >= 0 ? 1u : 0u;
}
