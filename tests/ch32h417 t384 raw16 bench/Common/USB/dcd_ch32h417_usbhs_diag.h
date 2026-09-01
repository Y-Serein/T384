#ifndef DCD_CH32H417_USBHS_DIAG_H
#define DCD_CH32H417_USBHS_DIAG_H

#include <stdint.h>

typedef struct {
    uint32_t init_ok;
    uint32_t init_fail;
    uint32_t irq;
    uint32_t bus_reset;
    uint32_t link_ready;
    uint32_t transfer;
    uint32_t ep0_setup;
    uint32_t ep0_out;
    uint32_t ep0_in;
    uint32_t set_address;
    uint32_t unhandled;
    uint8_t control;
    uint8_t int_en;
    uint8_t int_fg;
    uint8_t int_st;
    uint8_t mis_st;
    uint8_t dev_ad;
} t384_usbhs_diag_t;

void t384_usbhs_diag_snapshot(t384_usbhs_diag_t *snapshot);

#endif
