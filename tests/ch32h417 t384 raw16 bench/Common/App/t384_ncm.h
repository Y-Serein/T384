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
} t384_ncm_stats_t;

void t384_ncm_prepare_identity(void);
bool t384_ncm_init(void);
void t384_ncm_task(void);
void t384_ncm_get_stats(t384_ncm_stats_t *out);

#endif
