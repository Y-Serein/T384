/*
 * TinyUSB device-controller driver for the CH32H417 USBHS peripheral.
 *
 * The CH32H417 register layout is not compatible with TinyUSB's CH32V307
 * driver. This port follows the CH32H417 EVT USBHS device examples for clock,
 * endpoint DMA, toggle and interrupt handling while exposing TinyUSB's DCD API.
 */
#include "tusb_option.h"

#if CFG_TUD_ENABLED && TU_CHECK_MCU(OPT_MCU_CH32H417)

#include <string.h>

#include "ch32h417.h"
#include "ch32h417_rcc.h"
#include "ch32h417_usb.h"
#include "dcd_ch32h417_usbhs_diag.h"
#include "device/dcd.h"
#include "t384_compiler.h"

#define H417_USBHS_EP_COUNT 8u
#define H417_USBHS_MAX_PACKET 512u
#define H417_USBHS_PLL_TIMEOUT 1000000u

typedef struct {
    uint8_t *buffer;
    uint16_t total_len;
    uint16_t queued_len;
    uint16_t max_packet;
    bool active;
} h417_xfer_t;

static h417_xfer_t xfers[H417_USBHS_EP_COUNT][2];
TU_ATTR_ALIGNED(4) static uint8_t ep0_buffer[CFG_TUD_ENDPOINT0_SIZE];
static volatile t384_usbhs_diag_t usbhs_diag;

#define EP_TX_LEN(ep) \
    (*(volatile uint16_t *)((uintptr_t)&USBHSD->UEP0_TX_LEN + ((uintptr_t)(ep) * 4u)))
#define EP_TX_CTRL(ep) \
    (*(volatile uint8_t *)((uintptr_t)&USBHSD->UEP0_TX_CTRL + ((uintptr_t)(ep) * 4u)))
#define EP_RX_CTRL(ep) \
    (*(volatile uint8_t *)((uintptr_t)&USBHSD->UEP0_RX_CTRL + ((uintptr_t)(ep) * 4u)))
#define EP_RX_LEN(ep) \
    (*(volatile uint16_t *)((uintptr_t)&USBHSD->UEP0_RX_LEN + ((uintptr_t)(ep) * 4u)))
#define EP_MAX_LEN(ep) \
    (*(volatile uint32_t *)((uintptr_t)&USBHSD->UEP0_MAX_LEN + ((uintptr_t)(ep) * 4u)))
#define EP_RX_DMA(ep) \
    (*(volatile uint32_t *)((uintptr_t)&USBHSD->UEP1_RX_DMA + (((uintptr_t)(ep) - 1u) * 4u)))
#define EP_TX_DMA(ep) \
    (*(volatile uint32_t *)((uintptr_t)&USBHSD->UEP1_TX_DMA + (((uintptr_t)(ep) - 1u) * 4u)))

static bool usbhs_clock_enable(void)
{
    if ((RCC->PLLCFGR & RCC_SYSPLL_SEL) != RCC_SYSPLL_USBHS) {
        RCC_USBHS_PLLCmd(DISABLE);
        RCC_USBHSPLLCLKConfig(RCC_USBHSPLLSource_HSE);
        RCC_USBHSPLLReferConfig(RCC_USBHSPLLRefer_25M);
        RCC_USBHSPLLClockSourceDivConfig(RCC_USBHSPLL_IN_Div1);
        RCC_USBHS_PLLCmd(ENABLE);
        uint32_t timeout = H417_USBHS_PLL_TIMEOUT;
        while ((RCC->CTLR & RCC_USBHS_PLLRDY) == 0u && timeout != 0u) {
            --timeout;
        }
        if (timeout == 0u) {
            RCC_USBHS_PLLCmd(DISABLE);
            return false;
        }
    }

    RCC_UTMIcmd(ENABLE);
    RCC_HBPeriphClockCmd(RCC_HBPeriph_USBHS, ENABLE);
    return true;
}

static void usbhs_clock_disable(void)
{
    RCC_HBPeriphClockCmd(RCC_HBPeriph_USBHS, DISABLE);
    RCC_UTMIcmd(DISABLE);
    if ((RCC->PLLCFGR & RCC_SYSPLL_SEL) != RCC_SYSPLL_USBHS) {
        RCC_USBHS_PLLCmd(DISABLE);
    }
}

static void set_tx_response(uint8_t ep, uint8_t response)
{
    EP_TX_CTRL(ep) = (uint8_t)((EP_TX_CTRL(ep) & ~USBHS_UEP_T_RES_MASK) | response);
}

static void set_rx_response(uint8_t ep, uint8_t response)
{
    EP_RX_CTRL(ep) = (uint8_t)((EP_RX_CTRL(ep) & ~USBHS_UEP_R_RES_MASK) | response);
}

static void reset_endpoint_state(void)
{
    memset(xfers, 0, sizeof(xfers));

    USBHSD->UEP_TX_EN = USBHS_UEP0_T_EN;
    USBHSD->UEP_RX_EN = USBHS_UEP0_R_EN;
    USBHSD->UEP_TX_TOG_AUTO = 0u;
    USBHSD->UEP_RX_TOG_AUTO = 0u;
    USBHSD->UEP_TX_ISO = 0u;
    USBHSD->UEP_RX_ISO = 0u;

    for (uint8_t ep = 0; ep < H417_USBHS_EP_COUNT; ++ep) {
        EP_TX_LEN(ep) = 0u;
        EP_TX_CTRL(ep) = USBHS_UEP_T_RES_NAK | USBHS_UEP_T_TOG_DATA0;
        EP_RX_CTRL(ep) = USBHS_UEP_R_RES_NAK | USBHS_UEP_R_TOG_DATA0;
        EP_MAX_LEN(ep) = 0u;
    }

    USBHSD->UEP0_DMA = (uint32_t)(uintptr_t)ep0_buffer;
    EP_MAX_LEN(0) = CFG_TUD_ENDPOINT0_SIZE;
    xfers[0][TUSB_DIR_OUT].max_packet = CFG_TUD_ENDPOINT0_SIZE;
    xfers[0][TUSB_DIR_IN].max_packet = CFG_TUD_ENDPOINT0_SIZE;
    EP_RX_CTRL(0) = USBHS_UEP_R_RES_ACK | USBHS_UEP_R_TOG_DATA0;
}

static void prepare_next_packet(uint8_t ep, tusb_dir_t dir)
{
    h417_xfer_t *xfer = &xfers[ep][dir];

    if (dir == TUSB_DIR_IN) {
        const uint16_t remaining = (uint16_t)(xfer->total_len - xfer->queued_len);
        const uint16_t count = TU_MIN(remaining, xfer->max_packet);

        if (ep == 0u) {
            if (count != 0u) {
                memcpy(ep0_buffer, xfer->buffer + xfer->queued_len, count);
            }
        } else {
            EP_TX_DMA(ep) = (uint32_t)(uintptr_t)
                (count != 0u && xfer->buffer != NULL
                     ? xfer->buffer + xfer->queued_len
                     : ep0_buffer);
        }

        EP_TX_LEN(ep) = count;
        xfer->queued_len = (uint16_t)(xfer->queued_len + count);
        set_tx_response(ep, USBHS_UEP_T_RES_ACK);
    } else {
        if (ep > 0u) {
            const uint16_t remaining = (uint16_t)(xfer->total_len - xfer->queued_len);
            EP_RX_DMA(ep) = (uint32_t)(uintptr_t)
                (remaining != 0u && xfer->buffer != NULL
                     ? xfer->buffer + xfer->queued_len
                     : ep0_buffer);
            EP_MAX_LEN(ep) = TU_MIN(remaining, xfer->max_packet);
        }
        set_rx_response(ep, USBHS_UEP_R_RES_ACK);
    }
}

bool dcd_init(uint8_t rhport, const tusb_rhport_init_t *rh_init)
{
    (void)rhport;
    (void)rh_init;

    if (!usbhs_clock_enable()) {
        ++usbhs_diag.init_fail;
        return false;
    }

    USBHSD->CONTROL = USBHS_UD_RST_LINK | USBHS_UD_PHY_SUSPENDM;
    USBHSD->INT_EN = 0u;
    USBHSD->INT_FG = 0xffu;
    reset_endpoint_state();
    USBHSD->DEV_AD = 0u;
    USBHSD->BASE_MODE = USBHS_UD_SPEED_HIGH;
    USBHSD->INT_EN = USBHS_UDIE_BUS_RST | USBHS_UDIE_SUSPEND |
                     USBHS_UDIE_BUS_SLEEP | USBHS_UDIE_TRANSFER |
                     USBHS_UDIE_LINK_RDY | USBHS_UDIE_FIFO_OVER;
    USBHSD->CONTROL = USBHS_UD_DEV_EN | USBHS_UD_DMA_EN |
                      USBHS_UD_LPM_EN | USBHS_UD_PHY_SUSPENDM;
    ++usbhs_diag.init_ok;

    return true;
}

bool dcd_deinit(uint8_t rhport)
{
    (void)rhport;
    NVIC_DisableIRQ(USBHS_IRQn);
    USBHSD->INT_EN = 0u;
    USBHSD->CONTROL = USBHS_UD_RST_SIE | USBHS_UD_RST_LINK;
    usbhs_clock_disable();
    return true;
}

void dcd_int_enable(uint8_t rhport)
{
    (void)rhport;
    NVIC_EnableIRQ(USBHS_IRQn);
}

void dcd_int_disable(uint8_t rhport)
{
    (void)rhport;
    NVIC_DisableIRQ(USBHS_IRQn);
}

void dcd_connect(uint8_t rhport)
{
    (void)rhport;
    USBHSD->CONTROL |= USBHS_UD_DEV_EN;
}

void dcd_disconnect(uint8_t rhport)
{
    (void)rhport;
    USBHSD->CONTROL &= (uint8_t)~USBHS_UD_DEV_EN;
}

void dcd_set_address(uint8_t rhport, uint8_t dev_addr)
{
    (void)dev_addr;
    ++usbhs_diag.set_address;
    (void)dcd_edpt_xfer(rhport, 0x80u, NULL, 0u);
}

void dcd_edpt0_status_complete(uint8_t rhport, tusb_control_request_t const *request)
{
    (void)rhport;
    if (request->bmRequestType_bit.recipient == TUSB_REQ_RCPT_DEVICE &&
        request->bmRequestType_bit.type == TUSB_REQ_TYPE_STANDARD &&
        request->bRequest == TUSB_REQ_SET_ADDRESS) {
        USBHSD->DEV_AD = (uint8_t)(request->wValue & USBHS_UD_DEV_ADDR);
    }

    xfers[0][TUSB_DIR_OUT].active = false;
    xfers[0][TUSB_DIR_IN].active = false;
    EP_TX_CTRL(0) = USBHS_UEP_T_RES_NAK | USBHS_UEP_T_TOG_DATA0;
    EP_RX_CTRL(0) = USBHS_UEP_R_RES_ACK | USBHS_UEP_R_TOG_DATA0;
}

void dcd_remote_wakeup(uint8_t rhport)
{
    (void)rhport;
    /* Remote wakeup is not advertised by the current configuration. */
}

void dcd_sof_enable(uint8_t rhport, bool enabled)
{
    (void)rhport;
    if (enabled) {
        USBHSD->INT_EN |= USBHS_UDIE_SOF_ACT;
    } else {
        USBHSD->INT_EN &= (uint8_t)~USBHS_UDIE_SOF_ACT;
    }
}

bool dcd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const *desc_ep)
{
    (void)rhport;
    const uint8_t ep = tu_edpt_number(desc_ep->bEndpointAddress);
    const tusb_dir_t dir = tu_edpt_dir(desc_ep->bEndpointAddress);

    TU_VERIFY(ep < H417_USBHS_EP_COUNT, false);
    if (ep == 0u) {
        return true;
    }

    h417_xfer_t *xfer = &xfers[ep][dir];
    memset(xfer, 0, sizeof(*xfer));
    xfer->max_packet = tu_edpt_packet_size(desc_ep);
    TU_VERIFY(xfer->max_packet <= H417_USBHS_MAX_PACKET, false);
    EP_MAX_LEN(ep) = xfer->max_packet;

    if (dir == TUSB_DIR_IN) {
        USBHSD->UEP_TX_EN |= (uint16_t)(1u << ep);
        USBHSD->UEP_TX_TOG_AUTO |= (uint16_t)(1u << ep);
        EP_TX_LEN(ep) = 0u;
        EP_TX_CTRL(ep) = USBHS_UEP_T_RES_NAK | USBHS_UEP_T_TOG_DATA0;
    } else {
        USBHSD->UEP_RX_EN |= (uint16_t)(1u << ep);
        USBHSD->UEP_RX_TOG_AUTO |= (uint16_t)(1u << ep);
        EP_RX_CTRL(ep) = USBHS_UEP_R_RES_NAK | USBHS_UEP_R_TOG_DATA0;
    }

    return true;
}

void dcd_edpt_close_all(uint8_t rhport)
{
    (void)rhport;
    reset_endpoint_state();
}

void dcd_edpt_close(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
    const uint8_t ep = tu_edpt_number(ep_addr);
    const tusb_dir_t dir = tu_edpt_dir(ep_addr);
    if (ep == 0u || ep >= H417_USBHS_EP_COUNT) {
        return;
    }

    xfers[ep][dir].active = false;
    if (dir == TUSB_DIR_IN) {
        set_tx_response(ep, USBHS_UEP_T_RES_NAK);
        USBHSD->UEP_TX_EN &= (uint16_t)~(1u << ep);
        USBHSD->UEP_TX_TOG_AUTO &= (uint16_t)~(1u << ep);
    } else {
        set_rx_response(ep, USBHS_UEP_R_RES_NAK);
        USBHSD->UEP_RX_EN &= (uint16_t)~(1u << ep);
        USBHSD->UEP_RX_TOG_AUTO &= (uint16_t)~(1u << ep);
    }
}

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes)
{
    (void)rhport;
    const uint8_t ep = tu_edpt_number(ep_addr);
    const tusb_dir_t dir = tu_edpt_dir(ep_addr);

    TU_VERIFY(ep < H417_USBHS_EP_COUNT, false);
    h417_xfer_t *xfer = &xfers[ep][dir];
    TU_VERIFY(!xfer->active, false);

    xfer->buffer = buffer;
    xfer->total_len = total_bytes;
    xfer->queued_len = 0u;
    xfer->active = true;
    prepare_next_packet(ep, dir);
    return true;
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
    const uint8_t ep = tu_edpt_number(ep_addr);
    const tusb_dir_t dir = tu_edpt_dir(ep_addr);
    if (ep >= H417_USBHS_EP_COUNT) {
        return;
    }

    xfers[ep][dir].active = false;
    if (dir == TUSB_DIR_IN) {
        EP_TX_LEN(ep) = 0u;
        set_tx_response(ep, USBHS_UEP_T_RES_STALL);
    } else {
        set_rx_response(ep, USBHS_UEP_R_RES_STALL);
    }
}

void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
    const uint8_t ep = tu_edpt_number(ep_addr);
    const tusb_dir_t dir = tu_edpt_dir(ep_addr);
    if (ep == 0u || ep >= H417_USBHS_EP_COUNT) {
        return;
    }

    if (dir == TUSB_DIR_IN) {
        EP_TX_CTRL(ep) = USBHS_UEP_T_RES_NAK | USBHS_UEP_T_TOG_DATA0;
    } else {
        EP_RX_CTRL(ep) = USBHS_UEP_R_RES_NAK | USBHS_UEP_R_TOG_DATA0;
    }
}

static void handle_setup(uint8_t rhport)
{
    ++usbhs_diag.ep0_setup;
    xfers[0][TUSB_DIR_OUT].active = false;
    xfers[0][TUSB_DIR_IN].active = false;
    EP_TX_CTRL(0) = USBHS_UEP_T_RES_NAK | USBHS_UEP_T_TOG_DATA1;
    EP_RX_CTRL(0) = USBHS_UEP_R_RES_NAK | USBHS_UEP_R_TOG_DATA1;
    dcd_event_setup_received(rhport, ep0_buffer, true);
}

static void handle_out(uint8_t rhport, uint8_t ep)
{
    if (ep == 0u) {
        ++usbhs_diag.ep0_out;
    }
    const uint8_t status = EP_RX_CTRL(ep);
    EP_RX_CTRL(ep) = (uint8_t)(status & ~USBHS_UEP_R_DONE);

    if (ep == 0u && (status & USBHS_UEP_R_SETUP_IS) != 0u) {
        handle_setup(rhport);
        return;
    }

    h417_xfer_t *xfer = &xfers[ep][TUSB_DIR_OUT];
    if (!xfer->active) {
        set_rx_response(ep, USBHS_UEP_R_RES_NAK);
        return;
    }

    /* WCH's EP0 status OUT does not reliably report TOG_MATCH.  The
     * controller already owns EP0's control toggle; only data endpoints need
     * this duplicate-packet guard. */
    if (ep > 0u && (status & USBHS_UEP_R_TOG_MATCH) == 0u) {
        set_rx_response(ep, USBHS_UEP_R_RES_ACK);
        return;
    }

    const uint16_t count = EP_RX_LEN(ep);
    const uint16_t room = (uint16_t)(xfer->total_len - xfer->queued_len);
    if (count > room || (count != 0u && xfer->buffer == NULL)) {
        xfer->active = false;
        set_rx_response(ep, USBHS_UEP_R_RES_NAK);
        dcd_event_xfer_complete(rhport, tu_edpt_addr(ep, TUSB_DIR_OUT),
                                xfer->queued_len, XFER_RESULT_FAILED, true);
        return;
    }
    if (count != 0u && ep == 0u) {
        memcpy(xfer->buffer + xfer->queued_len, ep0_buffer, count);
    }
    xfer->queued_len = (uint16_t)(xfer->queued_len + count);
    if (ep == 0u) {
        EP_RX_CTRL(ep) ^= USBHS_UEP_R_TOG_DATA1;
    }

    if (count < xfer->max_packet || xfer->queued_len >= xfer->total_len) {
        xfer->active = false;
        set_rx_response(ep, USBHS_UEP_R_RES_NAK);
        dcd_event_xfer_complete(rhport, tu_edpt_addr(ep, TUSB_DIR_OUT),
                                xfer->queued_len, XFER_RESULT_SUCCESS, true);
    } else {
        prepare_next_packet(ep, TUSB_DIR_OUT);
    }
}

static void handle_in(uint8_t rhport, uint8_t ep)
{
    if (ep == 0u) {
        ++usbhs_diag.ep0_in;
    }
    EP_TX_CTRL(ep) &= (uint8_t)~USBHS_UEP_T_DONE;
    h417_xfer_t *xfer = &xfers[ep][TUSB_DIR_IN];
    if (!xfer->active) {
        set_tx_response(ep, USBHS_UEP_T_RES_NAK);
        return;
    }

    if (ep == 0u) {
        EP_TX_CTRL(ep) ^= USBHS_UEP_T_TOG_DATA1;
    }
    if (xfer->queued_len >= xfer->total_len) {
        xfer->active = false;
        set_tx_response(ep, USBHS_UEP_T_RES_NAK);
        dcd_event_xfer_complete(rhport, tu_edpt_addr(ep, TUSB_DIR_IN),
                                xfer->queued_len, XFER_RESULT_SUCCESS, true);
    } else {
        prepare_next_packet(ep, TUSB_DIR_IN);
    }
}

void dcd_int_handler(uint8_t rhport)
{
    ++usbhs_diag.irq;
    uint8_t flags = USBHSD->INT_FG;

    if ((flags & USBHS_UDIF_BUS_RST) != 0u) {
        ++usbhs_diag.bus_reset;
        USBHSD->DEV_AD = 0u;
        reset_endpoint_state();
        USBHSD->INT_FG = USBHS_UDIF_BUS_RST;
        /* USBHS reports BUS_RST before its negotiated speed bit is stable.
         * BASE_MODE is deliberately HS for this firmware; reporting the
         * transient MIS_ST value made TinyUSB serve the FS descriptor during
         * an HS enumeration (Windows then reports Code 43). */
        dcd_event_bus_reset(rhport, TUSB_SPEED_HIGH, true);
        flags = (uint8_t)(flags & ~USBHS_UDIF_BUS_RST);
    }

    if ((flags & USBHS_UDIF_TRANSFER) != 0u) {
        ++usbhs_diag.transfer;
        const uint8_t status = USBHSD->INT_ST;
        const uint8_t ep = (uint8_t)(status & USBHS_UDIS_EP_ID_MASK);
        if (ep < H417_USBHS_EP_COUNT) {
            if ((status & USBHS_UDIS_EP_DIR) != 0u) {
                handle_in(rhport, ep);
            } else {
                handle_out(rhport, ep);
            }
        }
        USBHSD->INT_FG = USBHS_UDIF_TRANSFER;
        flags = (uint8_t)(flags & ~USBHS_UDIF_TRANSFER);
    }

    if ((flags & USBHS_UDIF_RX_SOF) != 0u) {
        dcd_event_sof(rhport, USBHSD->FRAME_NO & USBHS_UD_FRAME_NO, true);
        USBHSD->INT_FG = USBHS_UDIF_RX_SOF;
        flags = (uint8_t)(flags & ~USBHS_UDIF_RX_SOF);
    }

    if ((flags & USBHS_UDIF_SUSPEND) != 0u) {
        dcd_event_bus_signal(rhport,
                             (USBHSD->MIS_ST & USBHS_UDMS_SUSPEND) != 0u
                                 ? DCD_EVENT_SUSPEND
                                 : DCD_EVENT_RESUME,
                             true);
        USBHSD->INT_FG = USBHS_UDIF_SUSPEND;
        flags = (uint8_t)(flags & ~USBHS_UDIF_SUSPEND);
    }

    if ((flags & USBHS_UDIF_LINK_RDY) != 0u) {
        ++usbhs_diag.link_ready;
        USBHSD->INT_FG = USBHS_UDIF_LINK_RDY;
        flags = (uint8_t)(flags & ~USBHS_UDIF_LINK_RDY);
    }

    if (flags != 0u) {
        ++usbhs_diag.unhandled;
        USBHSD->INT_FG = flags;
    }
}

void t384_usbhs_diag_snapshot(t384_usbhs_diag_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    snapshot->init_ok = usbhs_diag.init_ok;
    snapshot->init_fail = usbhs_diag.init_fail;
    snapshot->irq = usbhs_diag.irq;
    snapshot->bus_reset = usbhs_diag.bus_reset;
    snapshot->link_ready = usbhs_diag.link_ready;
    snapshot->transfer = usbhs_diag.transfer;
    snapshot->ep0_setup = usbhs_diag.ep0_setup;
    snapshot->ep0_out = usbhs_diag.ep0_out;
    snapshot->ep0_in = usbhs_diag.ep0_in;
    snapshot->set_address = usbhs_diag.set_address;
    snapshot->unhandled = usbhs_diag.unhandled;
    snapshot->control = USBHSD->CONTROL;
    snapshot->int_en = USBHSD->INT_EN;
    snapshot->int_fg = USBHSD->INT_FG;
    snapshot->int_st = USBHSD->INT_ST;
    snapshot->mis_st = USBHSD->MIS_ST;
    snapshot->dev_ad = USBHSD->DEV_AD;
}

void USBHS_IRQHandler(void) T384_FAST_ISR;
void USBHS_IRQHandler(void)
{
    dcd_int_handler(0u);
}

#endif
