#include "t384_ncm.h"

#include <string.h>

#include "ch32h417.h"
#include "dhserver.h"
#include "http_status.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "t384_product_config.h"
#include "t384_dns.h"
#include "tusb.h"

#define INIT_IP4(a, b, c, d) { PP_HTONL(LWIP_MAKEU32(a, b, c, d)) }
#define ROM_CFG_USERADR_ID 0x1FFFF7E8u
#define T384_NCM_RX_BUDGET 8u

uint8_t tud_network_mac_address[6] = {0x02, 0x53, 0x49, 0x50, 0x34, 0x80};
char t384_usb_serial[13] = "025349503480";

static const ip4_addr_t device_ip = INIT_IP4(T384_NCM_IPV4_A, T384_NCM_IPV4_B,
                                              T384_NCM_IPV4_C, T384_NCM_DEVICE_HOST);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t no_gateway = INIT_IP4(0, 0, 0, 0);
static struct netif ncm_netif;
static struct pbuf *received_frame;
static uint16_t packet_filter;
static uint32_t receive_drop_count;
static bool network_initialized;
static volatile t384_ncm_stats_t ncm_stats;

static dhcp_entry_t dhcp_entries[T384_NCM_CLIENT_COUNT] = {
    {{0}, INIT_IP4(T384_NCM_IPV4_A, T384_NCM_IPV4_B, T384_NCM_IPV4_C, 2), 86400},
    {{0}, INIT_IP4(T384_NCM_IPV4_A, T384_NCM_IPV4_B, T384_NCM_IPV4_C, 3), 86400},
    {{0}, INIT_IP4(T384_NCM_IPV4_A, T384_NCM_IPV4_B, T384_NCM_IPV4_C, 4), 86400},
};

static const dhcp_config_t dhcp_config = {
    .router = INIT_IP4(T384_NCM_IPV4_A, T384_NCM_IPV4_B, T384_NCM_IPV4_C, T384_NCM_DEVICE_HOST),
    .port = 67,
    .dns = INIT_IP4(T384_NCM_IPV4_A, T384_NCM_IPV4_B, T384_NCM_IPV4_C, T384_NCM_DEVICE_HOST),
    .domain = NULL,
    .num_entry = T384_NCM_CLIENT_COUNT,
    .entries = dhcp_entries,
};

static void dns_receive(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                         const ip_addr_t *source, u16_t port)
{
    (void)arg;
    if (p == NULL) return;
    uint8_t packet[160];
    const uint8_t address[] = {T384_NCM_IPV4_A, T384_NCM_IPV4_B, T384_NCM_IPV4_C, T384_NCM_DEVICE_HOST};
    const size_t length = p->tot_len;
    size_t response_length = 0u;
    if (length <= sizeof(packet) - 16u && pbuf_copy_partial(p, packet, (u16_t)length, 0u) == length)
        response_length = t384_dns_reply(packet, length, sizeof(packet), address);
    pbuf_free(p);
    if (!response_length) return;
    struct pbuf *response = pbuf_alloc(PBUF_TRANSPORT, (u16_t)response_length, PBUF_RAM);
    if (response == NULL) return;
    if (pbuf_take(response, packet, (u16_t)response_length) == ERR_OK)
        (void)udp_sendto(pcb, response, source, port);
    pbuf_free(response);
}

static void discard_received_frame(void)
{
    if (received_frame != NULL) {
        pbuf_free(received_frame);
        received_frame = NULL;
    }
}

void t384_ncm_prepare_identity(void)
{
    const volatile uint8_t *rom_mac = (const volatile uint8_t *)(ROM_CFG_USERADR_ID + 5u);
    bool all_zero = true;
    bool all_ff = true;
    uint8_t candidate[6];

    for (unsigned i = 0; i < sizeof(candidate); ++i) {
        candidate[i] = rom_mac[-(int)i];
        all_zero = all_zero && candidate[i] == 0u;
        all_ff = all_ff && candidate[i] == 0xffu;
    }

    if (!all_zero && !all_ff) {
        memcpy(tud_network_mac_address, candidate, sizeof(candidate));
    } else {
        const uint32_t chip = DBGMCU_GetCHIPID();
        tud_network_mac_address[3] = (uint8_t)(chip >> 16);
        tud_network_mac_address[4] = (uint8_t)(chip >> 8);
        tud_network_mac_address[5] = (uint8_t)chip;
    }

    /* Locally administered, unicast. */
    tud_network_mac_address[0] =
        (uint8_t)((tud_network_mac_address[0] | 0x02u) & 0xfeu);

    static const char hex[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < sizeof(tud_network_mac_address); ++i) {
        t384_usb_serial[i * 2u] = hex[tud_network_mac_address[i] >> 4];
        t384_usb_serial[i * 2u + 1u] = hex[tud_network_mac_address[i] & 0x0fu];
    }
}

static err_t ncm_link_output(struct netif *netif, struct pbuf *p)
{
	(void)netif;
	++ncm_stats.tx_calls;

    /*
     * DHCP OFFER/ACK and ARP replies are short-lived control packets.  The
     * NCM class can briefly have all IN NTB buffers in flight; give TinyUSB a
     * bounded chance to retire one before reporting back-pressure to lwIP.
     * This runs from the main loop, never from the USB ISR.
     */
    for (unsigned attempt = 0u; attempt < 8u; ++attempt) {
		if (!tud_ready()) {
			++ncm_stats.tx_not_ready;
			return ERR_USE;
		}
		if (tud_network_can_xmit(p->tot_len)) {
			tud_network_xmit(p, 0u);
			++ncm_stats.tx_submit;
			return ERR_OK;
		}
		++ncm_stats.tx_backpressure;
		tud_task();
	}

	++ncm_stats.tx_drop;
	return ERR_MEM;
}

static err_t ncm_netif_init(struct netif *netif)
{
    netif->mtu = CFG_TUD_NET_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    netif->name[0] = 'N';
    netif->name[1] = 'C';
    netif->linkoutput = ncm_link_output;
    netif->output = etharp_output;
    return ERR_OK;
}

bool t384_ncm_init(void)
{
    lwip_init();

    ncm_netif.hwaddr_len = sizeof(tud_network_mac_address);
    memcpy(ncm_netif.hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
    ncm_netif.hwaddr[5] ^= 0x01u;

    if (netif_add(&ncm_netif, &device_ip, &netmask, &no_gateway, NULL,
                  ncm_netif_init, ethernet_input) == NULL) {
        return false;
    }
    netif_set_default(&ncm_netif);
    netif_set_up(&ncm_netif);
    if (tud_mounted()) {
        netif_set_link_up(&ncm_netif);
    } else {
        netif_set_link_down(&ncm_netif);
    }

    if (dhserv_init(&dhcp_config) != ERR_OK) {
        return false;
    }
    for (unsigned i = 0u; i < T384_NCM_CLIENT_COUNT; ++i) {
        IP4_ADDR(&dhcp_entries[i].addr, T384_NCM_IPV4_A, T384_NCM_IPV4_B,
                 T384_NCM_IPV4_C, T384_NCM_FIRST_CLIENT_HOST + i);
        dhcp_entries[i].lease = 86400u;
    }
    struct udp_pcb *dns = udp_new_ip_type(IPADDR_TYPE_V4);
    if (dns == NULL) return false;
    if (udp_bind(dns, (const ip_addr_t *)&device_ip, 53u) != ERR_OK) {
        udp_remove(dns); return false;
    }
    udp_recv(dns, dns_receive, NULL);
    if (t384_http_status_init() != ERR_OK) {
        return false;
    }
    network_initialized = true;
    return true;
}

void t384_ncm_task(void)
{
    /*
     * One USB OUT NTB can contain up to six Ethernet datagrams.  Renewing
     * reception may therefore publish another TCP ACK immediately.  Drain a
     * complete NTB in the same main-loop pass instead of adding one loop of
     * latency per ACK; keep a hard budget so RX cannot starve capture or TX.
     */
    for (unsigned handled = 0u;
         handled < T384_NCM_RX_BUDGET && received_frame != NULL;
         ++handled) {
        struct pbuf *frame = received_frame;
        received_frame = NULL;
        if (ethernet_input(frame, &ncm_netif) != ERR_OK) {
            pbuf_free(frame);
        }
        tud_network_recv_renew();
    }
    sys_check_timeouts();
    t384_http_status_task();
    /* Flush once after ACK processing, timers and HTTP have all had a chance
     * to queue adjacent Ethernet frames into the same NCM NTB. This keeps
     * DHCP/ARP latency bounded to one main-loop pass while allowing RAW16 TCP
     * traffic to use the USBHS burst transfer added by this bench. */
    tud_network_xmit_flush();
}

bool tud_network_recv_cb(const uint8_t *source, uint16_t size)
{
	++ncm_stats.rx_callback;
	if (received_frame != NULL) {
		++ncm_stats.rx_busy_drop;
		return false;
    }
    if (size == 0u) {
        return true;
    }

    struct pbuf *frame = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
	if (frame == NULL) {
		++receive_drop_count;
		++ncm_stats.rx_alloc_drop;
		return true;
	}
	if (pbuf_take(frame, source, size) != ERR_OK) {
		pbuf_free(frame);
		++receive_drop_count;
		++ncm_stats.rx_take_drop;
        return true;
    }
    received_frame = frame;
    return true;
}

void t384_ncm_get_stats(t384_ncm_stats_t *out)
{
	if (out == NULL) return;
	tud_network_ncm_diag_t diag;
	tud_network_ncm_diag_get(&diag);
	out->rx_callback = ncm_stats.rx_callback;
	out->rx_busy_drop = ncm_stats.rx_busy_drop;
	out->rx_alloc_drop = ncm_stats.rx_alloc_drop;
	out->rx_take_drop = ncm_stats.rx_take_drop;
	out->tx_calls = ncm_stats.tx_calls;
	out->tx_not_ready = ncm_stats.tx_not_ready;
	out->tx_backpressure = ncm_stats.tx_backpressure;
	out->tx_drop = ncm_stats.tx_drop;
	out->tx_submit = ncm_stats.tx_submit;
    out->mounts = ncm_stats.mounts;
    out->umounts = ncm_stats.umounts;
    out->suspends = ncm_stats.suspends;
    out->resumes = ncm_stats.resumes;
	out->xmit_max_ntb_size = diag.xmit_max_ntb_size;
	out->xmit_max_datagrams = diag.xmit_max_datagrams;
	out->xmit_free_ntb = diag.xmit_free_ntb;
	out->xmit_ready_ntb = diag.xmit_ready_ntb;
	out->xmit_glue_active = diag.xmit_glue_active;
	out->xmit_tinyusb_active = diag.xmit_tinyusb_active;
	out->xmit_glue_datagrams = diag.xmit_glue_datagrams;
	out->xmit_ntb_submit = diag.xmit_ntb_submit;
	out->xmit_ntb_complete = diag.xmit_ntb_complete;
	out->xmit_ntb_errors = diag.xmit_ntb_errors;
	out->xmit_ntb_bytes = diag.xmit_ntb_bytes;
	out->xmit_ntb_datagrams = diag.xmit_ntb_datagrams;
	out->xmit_ntb_1 = diag.xmit_ntb_1;
	out->xmit_ntb_2_4 = diag.xmit_ntb_2_4;
	out->xmit_ntb_5_8 = diag.xmit_ntb_5_8;
	out->xmit_ntb_9_plus = diag.xmit_ntb_9_plus;
}

uint16_t tud_network_xmit_cb(uint8_t *destination, void *reference, uint16_t argument)
{
    (void)argument;
    struct pbuf *frame = (struct pbuf *)reference;
    return pbuf_copy_partial(frame, destination, frame->tot_len, 0u);
}

void tud_network_init_cb(void)
{
    discard_received_frame();
    packet_filter = 0u;
}

void tud_mount_cb(void)
{
    ++ncm_stats.mounts;
    if (network_initialized) {
        /* Mount runs from the SET_CONFIGURATION control callback. Keep it
         * side-effect free for the HTTP/lwIP owner; an explicit unmount is
         * the session cleanup boundary. */
        netif_set_link_up(&ncm_netif);
    }
}

void tud_umount_cb(void)
{
    ++ncm_stats.umounts;
    if (network_initialized) {
        netif_set_link_down(&ncm_netif);
        discard_received_frame();
        t384_http_status_reset();
    }
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    ++ncm_stats.suspends;
    (void)remote_wakeup_en;
    if (network_initialized) {
        netif_set_link_down(&ncm_netif);
    }
}

void tud_resume_cb(void)
{
    ++ncm_stats.resumes;
    if (network_initialized && tud_mounted()) {
        netif_set_link_up(&ncm_netif);
    }
}

void tud_network_set_packet_filter_cb(uint16_t filter)
{
    packet_filter = filter;
}
