#ifndef T384_LWIPOPTS_H
#define T384_LWIPOPTS_H

#include "t384_raw16.h"
#include "t384_v5f_net_memory.h"

/* ESP-IDF carries lwIP 2.2.0 with optional extensions. Keep them disabled. */
#define ESP_LWIP 0
#define ESP_LWIP_ARP 0
#define ESP_LWIP_DHCP_FINE_TIMERS_ONDEMAND 0
#define ESP_LWIP_DNS_TIMERS_ONDEMAND 0
#define ESP_LWIP_IGMP_TIMERS_ONDEMAND 0
#define ESP_LWIP_IP4_REASSEMBLY_TIMERS_ONDEMAND 0
#define ESP_LWIP_IP6_REASSEMBLY_TIMERS_ONDEMAND 0
#define ESP_LWIP_MLD6_TIMERS_ONDEMAND 0

#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_TCPIP_CORE_LOCKING 0
#define LWIP_TIMERS 1

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_IGMP 0
#define LWIP_DHCP 0
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_IP_ACCEPT_UDP_PORT(dst_port) \
    ((dst_port) == PP_NTOHS(LWIP_IANA_PORT_DHCP_SERVER))
#define LWIP_AUTOIP 0
#define LWIP_DNS 0
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_RAW 1

#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define LWIP_STATS 0
#define LWIP_SNMP 0
#define LWIP_MDNS_RESPONDER 0
#define LWIP_NETIF_HOSTNAME 0
#define LWIP_NETIF_STATUS_CALLBACK 0
#define LWIP_NETIF_LINK_CALLBACK 0

#define MEM_ALIGNMENT 4
#if T384_NETWORK_ON_V5F && defined(Core_V5F)
/* The V5F linker gives lwIP a dedicated shared-data heap.  Keep the pool
 * footprint bounded; the high-rate TCP path is the only workload there. */
#define MEM_SIZE T384_V5F_LWIP_HEAP_BYTES
#define LWIP_RAM_HEAP_POINTER t384_v5f_lwip_heap
#define MEMP_NUM_TCP_SEG 64
#define PBUF_POOL_SIZE 4
#else
#define MEM_SIZE (96u * 1024u)
#define MEMP_NUM_TCP_SEG 96
#define PBUF_POOL_SIZE 16
#endif
#define MEMP_NUM_PBUF 32
#define MEMP_NUM_RAW_PCB 4
#define MEMP_NUM_UDP_PCB 6
#define MEMP_NUM_TCP_PCB 8
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define PBUF_POOL_BUFSIZE 1600

#define TCP_MSS 1460
#if T384_NETWORK_ON_V5F && defined(Core_V5F)
/* V5F uses a four-buffer RX pool to preserve SRAM for the TX data plane.
 * The image stream is device->host, so the smaller device RX window does not
 * reduce the outgoing TCP window; it only bounds host->device control data. */
#define TCP_WND (4 * TCP_MSS)
#else
#define TCP_WND (16 * TCP_MSS)
#endif
#if T384_RAW16_PROFILE == 640u
/* More outstanding COPY data for the larger native picture frames. The
 * existing 96 KiB heap and 64-pbuf queue bound the allocation; no new RAM bank. */
#if T384_NETWORK_ON_V5F && defined(Core_V5F)
#define TCP_SND_BUF (16 * TCP_MSS)
#else
#define TCP_SND_BUF (32 * TCP_MSS)
#endif
#else
#define TCP_SND_BUF (16 * TCP_MSS)
#endif
#define TCP_SND_QUEUELEN 64
#define TCP_LISTEN_BACKLOG 1
#define TCP_QUEUE_OOSEQ 0

#define IP_REASSEMBLY 0
#define IP_FRAG 0
#define LWIP_CHKSUM_ALGORITHM 3
#define CHECKSUM_GEN_IP 1
#define CHECKSUM_GEN_UDP 1
#define CHECKSUM_GEN_TCP 1
#define CHECKSUM_CHECK_IP 1
#define CHECKSUM_CHECK_UDP 1
#define CHECKSUM_CHECK_TCP 1
#define LWIP_CHECKSUM_ON_COPY 1

#define LWIP_PROVIDE_ERRNO 1
#define LWIP_PLATFORM_DIAG(x) do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { (void)(x); NVIC_SystemReset(); } while (0)

#endif
