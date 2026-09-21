#ifndef T384_NCM_H
#define T384_NCM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct t384_ncm_stats
{
	uint32_t rx_callback;
	uint32_t rx_busy_drop;
	uint32_t rx_alloc_drop;
	uint32_t rx_take_drop;
	uint32_t tx_calls;
	uint32_t tx_not_ready;
	uint32_t tx_backpressure;
	uint32_t tx_drop;
	uint32_t tx_submit;
	uint32_t mounts;
	uint32_t umounts;
	uint32_t suspends;
	uint32_t resumes;
	uint32_t xmit_max_ntb_size;
	uint32_t xmit_max_datagrams;
	uint32_t xmit_free_ntb;
	uint32_t xmit_ready_ntb;
	uint32_t xmit_glue_active;
	uint32_t xmit_tinyusb_active;
	uint32_t xmit_glue_datagrams;
	uint32_t xmit_ntb_submit;
	uint32_t xmit_ntb_complete;
	uint32_t xmit_ntb_errors;
	uint32_t xmit_ntb_bytes;
	uint32_t xmit_ntb_datagrams;
	uint32_t xmit_ntb_1;
	uint32_t xmit_ntb_2_4;
	uint32_t xmit_ntb_5_8;
	uint32_t xmit_ntb_9_plus;
} t384_ncm_stats_t;

void t384_ncm_prepare_identity(void);
bool t384_ncm_init(void);
void t384_ncm_task(void);
void t384_ncm_get_stats(t384_ncm_stats_t *out);

#endif
