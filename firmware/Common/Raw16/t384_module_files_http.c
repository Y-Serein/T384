#include "t384_module_files_http.h"
#include <stdbool.h>
#include <string.h>

static bool equal(const char *a, size_t n, const char *b)
{ return n == strlen(b) && memcmp(a, b, n) == 0; }
static bool header(const char *a, size_t n, const char *b)
{
    if (n != strlen(b)) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + ('a'-'A') : a[i];
        if (c != b[i]) return false;
    }
    return true;
}
static bool number(const char *p, size_t n, uint32_t *out)
{
    if (!n) return false;
    uint32_t value = 0;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9' || value > (UINT32_MAX-(uint32_t)(p[i]-'0'))/10u)
            return false;
        value = value*10u + (uint32_t)(p[i]-'0');
    }
    *out = value; return true;
}

int t384_module_files_parse_http(const char *data, size_t size, t384_mf_request_t *out)
{
    if (!data || !out || memchr(data, 0, size)) return 400;
    if (size >= 4096u) return 413;
    /* Caller provides a NUL terminator at data[size]. */
    const char *end = strstr(data, "\r\n\r\n");
    if (!end) return 0;
    const char *line = strstr(data, "\r\n");
    const char *space = memchr(data, ' ', (size_t)(line-data));
    if (!space) return 400;
    const bool post = equal(data, (size_t)(space-data), "POST");
    if (!post && !equal(data, (size_t)(space-data), "GET")) return 405;
    const char *path = space+1;
    space = memchr(path, ' ', (size_t)(line-path));
    if (!space || (!equal(space+1, (size_t)(line-space-1), "HTTP/1.0") &&
                   !equal(space+1, (size_t)(line-space-1), "HTTP/1.1"))) return 400;
    const size_t path_len = (size_t)(space-path);
    memset(out, 0, sizeof(*out));
    if (!post && equal(path, path_len, "/api/v1/module-files/status")) out->action = T384_MF_HTTP_STATUS;
    else if (post && equal(path, path_len, "/api/v1/module-files/read")) out->action = T384_MF_HTTP_READ;
    else if (post && equal(path, path_len, "/api/v1/module-files/abort")) out->action = T384_MF_HTTP_ABORT;
    else {
        static const char prefix[] = "/api/v1/module-files/data?transaction=";
        if (post || path_len <= sizeof(prefix)-1u ||
            memcmp(path, prefix, sizeof(prefix)-1u) != 0 ||
            !number(path+sizeof(prefix)-1u, path_len-(sizeof(prefix)-1u), &out->transaction) ||
            out->transaction == 0u) return 404;
        out->action = T384_MF_HTTP_DATA;
    }
    uint32_t content_length = 0;
    bool has_length = false, has_type = false;
    for (const char *p = line+2; p < end; ) {
        const char *next = strstr(p, "\r\n");
        if (!next || next > end) return 400;
        const char *colon = memchr(p, ':', (size_t)(next-p));
        if (!colon || colon == p || *p == ' ' || *p == '\t') return 400;
        const char *value = colon+1;
        while (value < next && (*value == ' ' || *value == '\t')) ++value;
        const char *tail = next;
        while (tail > value && (tail[-1] == ' ' || tail[-1] == '\t')) --tail;
        const size_t name_len = (size_t)(colon-p), value_len = (size_t)(tail-value);
        if (header(p, name_len, "transfer-encoding") || header(p, name_len, "origin") ||
            header(p, name_len, "referer")) return 403;
        if (header(p, name_len, "content-length")) {
            if (has_length || !number(value, value_len, &content_length)) return 400;
            has_length = true;
        }
        if (header(p, name_len, "content-type")) {
            if (has_type || !equal(value, value_len, "application/octet-stream")) return 415;
            has_type = true;
        }
        p = next+2;
    }
    if (post && (!has_length || !has_type)) return 400;
    if (content_length > 15u || (!post && content_length != 0u)) return 413;
    const char *body = end+4;
    const size_t have = size-(size_t)(body-data);
    if (have < content_length) return 0;
    if (have != content_length) return 400;
    if (out->action == T384_MF_HTTP_READ) {
        if (content_length == 0u) return 400;
        memcpy(out->id, body, content_length);
    } else if (out->action == T384_MF_HTTP_ABORT) {
        if (!number(body, content_length, &out->transaction) || out->transaction == 0u) return 400;
    }
    return 200;
}
