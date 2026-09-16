#ifndef T384_MODULE_FILES_HTTP_H
#define T384_MODULE_FILES_HTTP_H
#include <stddef.h>
#include <stdint.h>
enum { T384_MF_HTTP_STATUS, T384_MF_HTTP_READ, T384_MF_HTTP_DATA, T384_MF_HTTP_ABORT };
typedef struct { unsigned action; uint32_t transaction; char id[16]; } t384_mf_request_t;
/* 0 incomplete, 200 parsed, other positive HTTP code is a rejection. */
int t384_module_files_parse_http(const char *data, size_t size, t384_mf_request_t *out);
#endif
