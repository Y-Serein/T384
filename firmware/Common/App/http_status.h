#ifndef T384_HTTP_STATUS_H
#define T384_HTTP_STATUS_H

#include "lwip/err.h"

err_t t384_http_status_init(void);
void t384_http_status_task(void);
void t384_http_status_reset(void);

#endif
