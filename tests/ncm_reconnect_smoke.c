/* Mount must not abort live HTTP/lwIP state from the SET_CONFIGURATION
 * callback. Session cleanup remains owned by umount. */
#include <assert.h>
#include <stdio.h>
#include "../firmware/Common/App/t384_ncm.c"

static unsigned freed, resets, sessions;
static bool mounted = true;
static unsigned usb_calls;
u8_t pbuf_free(struct pbuf *p) { assert(p != NULL); ++freed; return 1u; }
void netif_set_link_down(struct netif *netif) { netif->flags &= (u8_t)~NETIF_FLAG_LINK_UP; }
void netif_set_link_up(struct netif *netif) { netif->flags |= NETIF_FLAG_LINK_UP; }
bool tud_mounted(void) { return mounted; }
bool tud_suspended(void) { return false; }
bool tud_network_can_xmit(uint16_t size) { (void)size; ++usb_calls; return false; }
void tud_network_xmit(void *p, uint16_t arg) { (void)p; (void)arg; ++usb_calls; }
void tud_task_ext(uint32_t timeout_ms, bool in_isr) { (void)timeout_ms; (void)in_isr; ++usb_calls; }
void t384_http_status_reset(void) {
    ++resets;
    sessions = 0u;
}

int main(void) {
    struct pbuf pending;
    tud_mount_cb();
    assert(resets == 0u); /* USB may configure before lwIP initialization. */
    network_initialized = true;
    netif_set_link_up(&ncm_netif);
    received_frame = &pending;
    packet_filter = 0x0fu;
    sessions = 1u;
    tud_mount_cb(); /* BUS_RESET, then SET_CONFIGURATION, no UNPLUGGED. */
    assert(sessions == 1u && resets == 0u);
    assert(received_frame == &pending && freed == 0u && packet_filter == 0x0fu);
    assert(netif_is_link_up(&ncm_netif));
    sessions = 1u;
    tud_suspend_cb(false);
    assert(!netif_is_link_up(&ncm_netif) && sessions == 1u);
    tud_resume_cb();
    assert(netif_is_link_up(&ncm_netif) && sessions == 1u); /* Suspend is not unplug. */
    received_frame = &pending;
    tud_umount_cb();
    assert(sessions == 0u && resets == 1u && freed == 1u);
    assert(!netif_is_link_up(&ncm_netif));
    tud_mount_cb();
    assert(resets == 1u && freed == 1u && netif_is_link_up(&ncm_netif));
    mounted = false;
    tud_suspend_cb(false);
    tud_resume_cb();
    assert(!netif_is_link_up(&ncm_netif));
    puts("NCM mount without umount/stale RX/session cleanup/suspend/resume/remount passed");
    return 0;
}
