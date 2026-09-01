/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 by Sergey Fetisov <fsenok@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * version: 1.0 demo (7.02.2015)
 * brief:   tiny dhcp ipv4 server using lwip (pcb)
 * ref:     https://lists.gnu.org/archive/html/lwip-users/2012-12/msg00016.html
 */

#ifndef DHSERVER_H
#define DHSERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lwip/err.h"
#include "lwip/udp.h"
#include "netif/etharp.h"

typedef struct dhcp_entry
{
	uint8_t    mac[6];
	ip4_addr_t addr;
	uint32_t   lease;
} dhcp_entry_t;

typedef struct dhcp_config
{
	ip4_addr_t    router;
	uint16_t      port;
	ip4_addr_t    dns;
	const char   *domain;
	int           num_entry;
	dhcp_entry_t *entries;
} dhcp_config_t;

typedef struct dhserv_stats
{
	uint32_t rx;
	uint32_t no_netif;
	uint32_t malformed;
	uint32_t discover;
	uint32_t offer_attempt;
	uint32_t offer_pbuf_fail;
	uint32_t offer_ok;
	uint32_t offer_err;
	uint32_t no_entry;
	uint32_t request;
	uint32_t request_no_ip;
	uint32_t request_unknown_ip;
	uint32_t request_busy;
	uint32_t ack_attempt;
	uint32_t ack_pbuf_fail;
	uint32_t ack_ok;
	uint32_t ack_err;
} dhserv_stats_t;

#ifdef __cplusplus
extern "C" {
#endif
err_t dhserv_init(const dhcp_config_t *c);
void dhserv_free(void);
void dhserv_get_stats(dhserv_stats_t *out);
#ifdef __cplusplus
}
#endif

#endif /* DHSERVER_H */
