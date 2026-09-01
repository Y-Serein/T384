#include "http_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "dhserver.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "t384_ncm.h"
#include "t384_camera.h"
#include "t384_product_config.h"
#include "t384_time.h"

#define HTTP_CLIENTS 3u
#define HTTP_POLL_INTERVAL 4u
#define HTTP_IDLE_POLL_LIMIT 5u
#define HTTP_STREAM_WRITE_BUDGET 8u
#define HTTP_STREAM_STALL_TIMEOUT_MS 1000u

typedef enum {
    STREAM_PHASE_NONE = 0,
    STREAM_PHASE_HTTP_HEADER,
    STREAM_PHASE_IDLE,
    STREAM_PHASE_PART_HEADER,
    STREAM_PHASE_FRAME,
    STREAM_PHASE_TAIL,
} stream_phase_t;

typedef struct {
    struct tcp_pcb *pcb;
    uint8_t idle_polls;
    bool closing;
    bool static_response;
    bool image_response;
    bool stream_response;
    bool stream_blocked;
    stream_phase_t stream_phase;
    const char *static_data;
    size_t static_length;
    size_t static_offset;
    u16_t static_inflight;
    size_t header_length;
    size_t header_offset;
    const uint8_t *frame;
    uint32_t frame_length;
    uint32_t frame_offset;
    u16_t image_inflight;
    uint8_t tail_offset;
    uint32_t stream_last_progress_ms;
    char header[192];
} http_client_t;

typedef struct {
    uint32_t connects;
    uint32_t disconnects;
    uint32_t frames;
    uint32_t bytes;
    uint32_t backpressure;
    uint32_t write_errors;
    uint32_t timeout_disconnects;
    uint32_t fps_x1000;
} http_stream_stats_t;

static http_client_t clients[HTTP_CLIENTS];
static struct tcp_pcb *http_listener;
static http_client_t *stream_client;
static volatile http_stream_stats_t stream_stats;
static uint32_t stream_fps_started_ms;
static uint32_t stream_fps_frames;
static char diag_response[2048];
static const char stream_tail[] = "\r\n";

static const char status_response[] =
#include "device_console_html.inc"
;

static const char not_found_response[] =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Connection: close\r\n\r\nnot found\n";

static size_t build_diag_response(void)
{
    t384_ncm_stats_t ncm;
    dhserv_stats_t dhcp;
    t384_camera_stats_t camera;
    t384_ncm_get_stats(&ncm);
    dhserv_get_stats(&dhcp);
    t384_camera_get_stats(&camera);

    enum { DIAG_HEADER_RESERVE = 192 };
    const int body_length = snprintf(
        diag_response + DIAG_HEADER_RESERVE,
        sizeof(diag_response) - DIAG_HEADER_RESERVE,
        "ncm.rx_callback=%lu\n"
        "ncm.rx_busy_drop=%lu\n"
        "ncm.rx_alloc_drop=%lu\n"
        "ncm.rx_take_drop=%lu\n"
        "ncm.tx_calls=%lu\n"
        "ncm.tx_not_ready=%lu\n"
        "ncm.tx_backpressure=%lu\n"
        "ncm.tx_drop=%lu\n"
        "ncm.tx_submit=%lu\n"
        "dhcp.rx=%lu\n"
        "dhcp.no_netif=%lu\n"
        "dhcp.malformed=%lu\n"
        "dhcp.discover=%lu\n"
        "dhcp.offer_attempt=%lu\n"
        "dhcp.offer_pbuf_fail=%lu\n"
        "dhcp.offer_ok=%lu\n"
        "dhcp.offer_err=%lu\n"
        "dhcp.no_entry=%lu\n"
        "dhcp.request=%lu\n"
        "dhcp.request_no_ip=%lu\n"
        "dhcp.request_unknown_ip=%lu\n"
        "dhcp.request_busy=%lu\n"
        "dhcp.ack_attempt=%lu\n"
        "dhcp.ack_pbuf_fail=%lu\n"
        "dhcp.ack_ok=%lu\n"
        "dhcp.ack_err=%lu\n"
        "camera.initialized=%u\n"
        "camera.init_attempts=%lu\n"
        "camera.init_ok=%lu\n"
        "camera.init_fail=%lu\n"
        "camera.sensor_mid=0x%04lX\n"
        "camera.sensor_pid=0x%04lX\n"
        "camera.requests=%lu\n"
        "camera.starts=%lu\n"
        "camera.frame_starts=%lu\n"
        "camera.row_chunks=%lu\n"
        "camera.frame_done_irqs=%lu\n"
        "camera.stop_frame_irqs=%lu\n"
        "camera.frame_ends=%lu\n"
        "camera.fifo_overflows=%lu\n"
        "camera.frames=%lu\n"
        "camera.published_frames=%lu\n"
        "camera.bad_frames=%lu\n"
        "camera.overflows=%lu\n"
        "camera.timeouts=%lu\n"
        "camera.dropped_ready=%lu\n"
        "camera.dropped_no_slot=%lu\n"
        "camera.bytes=%lu\n"
        "camera.last_frame_bytes=%lu\n"
        "camera.max_frame_bytes=%lu\n"
        "camera.source_fps_x1000=%lu\n"
        "camera.active=%lu\n"
        "camera.ready=%lu\n"
        "camera.leased=%lu\n"
        "stream.active=%u\n"
        "stream.connects=%lu\n"
        "stream.disconnects=%lu\n"
        "stream.frames=%lu\n"
        "stream.bytes=%lu\n"
        "stream.backpressure=%lu\n"
        "stream.write_errors=%lu\n"
        "stream.timeout_disconnects=%lu\n"
        "stream.fps_x1000=%lu\n",
        (unsigned long)ncm.rx_callback,
        (unsigned long)ncm.rx_busy_drop,
        (unsigned long)ncm.rx_alloc_drop,
        (unsigned long)ncm.rx_take_drop,
        (unsigned long)ncm.tx_calls,
        (unsigned long)ncm.tx_not_ready,
        (unsigned long)ncm.tx_backpressure,
        (unsigned long)ncm.tx_drop,
        (unsigned long)ncm.tx_submit,
        (unsigned long)dhcp.rx,
        (unsigned long)dhcp.no_netif,
        (unsigned long)dhcp.malformed,
        (unsigned long)dhcp.discover,
        (unsigned long)dhcp.offer_attempt,
        (unsigned long)dhcp.offer_pbuf_fail,
        (unsigned long)dhcp.offer_ok,
        (unsigned long)dhcp.offer_err,
        (unsigned long)dhcp.no_entry,
        (unsigned long)dhcp.request,
        (unsigned long)dhcp.request_no_ip,
        (unsigned long)dhcp.request_unknown_ip,
        (unsigned long)dhcp.request_busy,
        (unsigned long)dhcp.ack_attempt,
        (unsigned long)dhcp.ack_pbuf_fail,
        (unsigned long)dhcp.ack_ok,
        (unsigned long)dhcp.ack_err,
        t384_camera_is_initialized() ? 1u : 0u,
        (unsigned long)camera.init_attempts,
        (unsigned long)camera.init_ok,
        (unsigned long)camera.init_fail,
        (unsigned long)camera.sensor_mid,
        (unsigned long)camera.sensor_pid,
        (unsigned long)camera.requests,
        (unsigned long)camera.starts,
        (unsigned long)camera.frame_starts,
        (unsigned long)camera.row_chunks,
        (unsigned long)camera.frame_done_irqs,
        (unsigned long)camera.stop_frame_irqs,
        (unsigned long)camera.frame_ends,
        (unsigned long)camera.fifo_overflows,
        (unsigned long)camera.frames,
        (unsigned long)camera.published_frames,
        (unsigned long)camera.bad_frames,
        (unsigned long)camera.overflows,
        (unsigned long)camera.timeouts,
        (unsigned long)camera.dropped_ready,
        (unsigned long)camera.dropped_no_slot,
        (unsigned long)camera.bytes,
        (unsigned long)camera.last_frame_bytes,
        (unsigned long)camera.max_frame_bytes,
        (unsigned long)camera.source_fps_x1000,
        (unsigned long)camera.active,
        (unsigned long)camera.ready,
        (unsigned long)camera.leased,
        stream_client != NULL ? 1u : 0u,
        (unsigned long)stream_stats.connects,
        (unsigned long)stream_stats.disconnects,
        (unsigned long)stream_stats.frames,
        (unsigned long)stream_stats.bytes,
        (unsigned long)stream_stats.backpressure,
        (unsigned long)stream_stats.write_errors,
        (unsigned long)stream_stats.timeout_disconnects,
        (unsigned long)stream_stats.fps_x1000);

    if (body_length < 0 ||
        (size_t)body_length >= sizeof(diag_response) - DIAG_HEADER_RESERVE) {
        return 0u;
    }

    char header[DIAG_HEADER_RESERVE];
    const int header_length = snprintf(
        header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: keep-alive\r\n"
        "Keep-Alive: timeout=10\r\n\r\n",
        (unsigned long)body_length);
    if (header_length < 0 || (size_t)header_length >= sizeof(header) ||
        (size_t)header_length + (size_t)body_length > sizeof(diag_response)) {
        return 0u;
    }
    memmove(diag_response + header_length,
            diag_response + DIAG_HEADER_RESERVE, (size_t)body_length);
    memcpy(diag_response, header, (size_t)header_length);
    return (size_t)header_length + (size_t)body_length;
}

static void release_client(http_client_t *client)
{
    if (client != NULL) {
        if (client->image_response ||
            (client->stream_response && client->frame != NULL)) {
            t384_camera_release_frame();
        }
        if (stream_client == client) {
            stream_client = NULL;
            ++stream_stats.disconnects;
            stream_stats.fps_x1000 = 0u;
        }
        client->pcb = NULL;
        client->idle_polls = 0u;
        client->closing = false;
        client->static_response = false;
        client->image_response = false;
        client->stream_response = false;
        client->stream_blocked = false;
        client->stream_phase = STREAM_PHASE_NONE;
        client->static_data = NULL;
        client->static_length = 0u;
        client->static_offset = 0u;
        client->static_inflight = 0u;
        client->header_length = 0u;
        client->header_offset = 0u;
        client->frame = NULL;
        client->frame_length = 0u;
        client->frame_offset = 0u;
        client->image_inflight = 0u;
        client->tail_offset = 0u;
        client->stream_last_progress_ms = 0u;
    }
}

static void abort_client(http_client_t *client)
{
    if (client == NULL || client->pcb == NULL) {
        return;
    }

    struct tcp_pcb *pcb = client->pcb;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_poll(pcb, NULL, 0u);
    tcp_err(pcb, NULL);
    release_client(client);
    tcp_abort(pcb);
}

static void http_error(void *arg, err_t error)
{
    (void)error;
    release_client((http_client_t *)arg);
}

static err_t close_client(http_client_t *client)
{
    const err_t error = tcp_close(client->pcb);
    if (error == ERR_OK) {
        release_client(client);
        return ERR_OK;
    }
    if (error == ERR_MEM) {
        client->closing = true;
        return ERR_OK;
    }

    abort_client(client);
    return ERR_ABRT;
}

static err_t queue_static_chunk(http_client_t *client)
{
    if (client->static_inflight != 0u) {
        return ERR_OK;
    }
    if (client->static_offset >= client->static_length) {
        client->closing = true;
        return close_client(client);
    }

    const size_t left = client->static_length - client->static_offset;
    const u16_t send_space = tcp_sndbuf(client->pcb);
    if (send_space == 0u) {
        return ERR_OK;
    }
    u16_t chunk = (u16_t)(left > 1460u ? 1460u : left);
    if (chunk > send_space) {
        chunk = send_space;
    }
    const err_t write_error = tcp_write(
        client->pcb, client->static_data + client->static_offset, chunk,
        TCP_WRITE_FLAG_COPY);
    if (write_error == ERR_MEM) {
        return ERR_OK;
    }
    if (write_error != ERR_OK) {
        abort_client(client);
        return ERR_ABRT;
    }

    client->static_offset += chunk;
    client->static_inflight = chunk;
    const err_t output_error = tcp_output(client->pcb);
    if (output_error != ERR_OK && output_error != ERR_MEM) {
        abort_client(client);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t queue_image_chunk(http_client_t *client)
{
    const void *data;
    u16_t chunk;
    if (client->header_offset < client->header_length) {
        const size_t left = client->header_length - client->header_offset;
        chunk = (u16_t)(left > 1460u ? 1460u : left);
        data = client->header + client->header_offset;
    } else if (client->frame_offset < client->frame_length) {
        const uint32_t left = client->frame_length - client->frame_offset;
        chunk = (u16_t)(left > 1460u ? 1460u : left);
        data = client->frame + client->frame_offset;
    } else {
        client->closing = true;
        return close_client(client);
    }

    const err_t write_error = tcp_write(client->pcb, data, chunk, TCP_WRITE_FLAG_COPY);
    if (write_error == ERR_MEM) {
        return ERR_OK;
    }
    if (write_error != ERR_OK) {
        abort_client(client);
        return ERR_ABRT;
    }

    if (client->header_offset < client->header_length) {
        client->header_offset += chunk;
    } else {
        client->frame_offset += chunk;
    }
    client->image_inflight = chunk;
    const err_t output_error = tcp_output(client->pcb);
    if (output_error != ERR_OK && output_error != ERR_MEM) {
        abort_client(client);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t queue_stream_data(http_client_t *client)
{
    bool wrote = false;
    for (unsigned write_count = 0u; write_count < HTTP_STREAM_WRITE_BUDGET;) {
        const void *data = NULL;
        size_t left = 0u;

        if (client->stream_phase == STREAM_PHASE_IDLE) {
            const uint8_t *frame = NULL;
            uint32_t frame_length = 0u;
            if (!t384_camera_acquire_frame(&frame, &frame_length)) {
                break;
            }
            const int header_length = snprintf(
                client->header, sizeof(client->header),
                "--t384frame\r\nContent-Type: image/jpeg\r\n"
                "Content-Length: %lu\r\n\r\n",
                (unsigned long)frame_length);
            if (header_length <= 0 ||
                (size_t)header_length >= sizeof(client->header)) {
                t384_camera_release_frame();
                ++stream_stats.write_errors;
                abort_client(client);
                return ERR_ABRT;
            }
            client->frame = frame;
            client->frame_length = frame_length;
            client->frame_offset = 0u;
            client->header_length = (size_t)header_length;
            client->header_offset = 0u;
            client->stream_phase = STREAM_PHASE_PART_HEADER;
        }

        if (client->stream_phase == STREAM_PHASE_HTTP_HEADER ||
            client->stream_phase == STREAM_PHASE_PART_HEADER) {
            if (client->header_offset == client->header_length) {
                client->stream_phase =
                    client->stream_phase == STREAM_PHASE_HTTP_HEADER
                        ? STREAM_PHASE_IDLE
                        : STREAM_PHASE_FRAME;
                continue;
            }
            data = client->header + client->header_offset;
            left = client->header_length - client->header_offset;
        } else if (client->stream_phase == STREAM_PHASE_FRAME) {
            if (client->frame_offset == client->frame_length) {
                const uint32_t sent_length = client->frame_length;
                t384_camera_release_frame();
                client->frame = NULL;
                client->frame_length = 0u;
                client->frame_offset = 0u;
                client->tail_offset = 0u;
                client->stream_phase = STREAM_PHASE_TAIL;
                ++stream_stats.frames;
                ++stream_fps_frames;
                stream_stats.bytes += sent_length;
                continue;
            }
            data = client->frame + client->frame_offset;
            left = client->frame_length - client->frame_offset;
        } else if (client->stream_phase == STREAM_PHASE_TAIL) {
            if (client->tail_offset == sizeof(stream_tail) - 1u) {
                client->stream_phase = STREAM_PHASE_IDLE;
                continue;
            }
            data = stream_tail + client->tail_offset;
            left = sizeof(stream_tail) - 1u - client->tail_offset;
        } else {
            break;
        }

        const u16_t send_space = tcp_sndbuf(client->pcb);
        if (send_space == 0u) {
            if (!client->stream_blocked) {
                ++stream_stats.backpressure;
                client->stream_blocked = true;
            }
            break;
        }
        size_t chunk_size = left;
        if (chunk_size > 1460u) {
            chunk_size = 1460u;
        }
        if (chunk_size > send_space) {
            chunk_size = send_space;
        }
        const u16_t chunk = (u16_t)chunk_size;
        const err_t write_error = tcp_write(client->pcb, data, chunk,
                                            TCP_WRITE_FLAG_COPY);
        if (write_error == ERR_MEM) {
            if (!client->stream_blocked) {
                ++stream_stats.backpressure;
                client->stream_blocked = true;
            }
            break;
        }
        if (write_error != ERR_OK) {
            ++stream_stats.write_errors;
            abort_client(client);
            return ERR_ABRT;
        }
        wrote = true;
        client->stream_blocked = false;
        client->stream_last_progress_ms = t384_millis();
        ++write_count;

        if (client->stream_phase == STREAM_PHASE_HTTP_HEADER ||
            client->stream_phase == STREAM_PHASE_PART_HEADER) {
            client->header_offset += chunk;
        } else if (client->stream_phase == STREAM_PHASE_FRAME) {
            client->frame_offset += chunk;
        } else {
            client->tail_offset = (uint8_t)(client->tail_offset + chunk);
        }
    }

    if (wrote) {
        const err_t output_error = tcp_output(client->pcb);
        if (output_error != ERR_OK && output_error != ERR_MEM) {
            ++stream_stats.write_errors;
            abort_client(client);
            return ERR_ABRT;
        }
    }
    return ERR_OK;
}

static err_t http_sent(void *arg, struct tcp_pcb *pcb, u16_t length)
{
    (void)pcb;
    (void)length;
    http_client_t *client = (http_client_t *)arg;
    client->idle_polls = 0u;
    if (client->static_response) {
        if (length < client->static_inflight) {
            client->static_inflight = (u16_t)(client->static_inflight - length);
            return ERR_OK;
        }
        client->static_inflight = 0u;
        return queue_static_chunk(client);
    }
    if (client->stream_response) {
        client->stream_last_progress_ms = t384_millis();
        return queue_stream_data(client);
    }
    if (client->image_response) {
        if (length < client->image_inflight) {
            client->image_inflight = (u16_t)(client->image_inflight - length);
            return ERR_OK;
        }
        client->image_inflight = 0u;
        return queue_image_chunk(client);
    }
    return client->closing ? close_client(client) : ERR_OK;
}

static err_t http_poll(void *arg, struct tcp_pcb *pcb)
{
    (void)pcb;
    http_client_t *client = (http_client_t *)arg;
    if (client->closing) {
        return close_client(client);
    }
    if (client->stream_response) {
        return queue_stream_data(client);
    }
    if (client->static_response && client->static_inflight == 0u) {
        const err_t result = queue_static_chunk(client);
        if (result != ERR_OK) {
            return result;
        }
        if (client->pcb == NULL || client->closing) {
            return ERR_OK;
        }
    }
    if (client->image_response && client->image_inflight == 0u) {
        const err_t result = queue_image_chunk(client);
        if (result != ERR_OK) {
            return result;
        }
        if (client->pcb == NULL || client->closing) {
            return ERR_OK;
        }
    }
    if (++client->idle_polls >= HTTP_IDLE_POLL_LIMIT) {
        abort_client(client);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t send_static_response(http_client_t *client, const char *response,
                                  size_t length)
{
    client->static_response = true;
    client->static_data = response;
    client->static_length = length;
    client->static_offset = 0u;
    client->static_inflight = 0u;
    client->idle_polls = 0u;
    return queue_static_chunk(client);
}

static err_t send_and_close(http_client_t *client, const char *response, size_t length)
{
    const err_t write_error = tcp_write(client->pcb, response, (u16_t)length,
                                        TCP_WRITE_FLAG_COPY);
    if (write_error != ERR_OK) {
        abort_client(client);
        return ERR_ABRT;
    }

    const err_t output_error = tcp_output(client->pcb);
    if (output_error != ERR_OK) {
        abort_client(client);
        return ERR_ABRT;
    }
    client->closing = true;
    return close_client(client);
}

static err_t send_keep_alive(http_client_t *client, const char *response, size_t length)
{
    if (length > tcp_sndbuf(client->pcb)) {
        abort_client(client);
        return ERR_ABRT;
    }
    const err_t write_error = tcp_write(client->pcb, response, (u16_t)length,
                                        TCP_WRITE_FLAG_COPY);
    if (write_error != ERR_OK) {
        abort_client(client);
        return ERR_ABRT;
    }
    const err_t output_error = tcp_output(client->pcb);
    if (output_error != ERR_OK && output_error != ERR_MEM) {
        abort_client(client);
        return ERR_ABRT;
    }
    client->idle_polls = 0u;
    return ERR_OK;
}

static err_t send_capture_unavailable(http_client_t *client, const char *reason)
{
    char response[256];
    const int length = snprintf(response, sizeof(response),
                                "HTTP/1.0 503 Service Unavailable\r\n"
                                "Content-Type: text/plain; charset=utf-8\r\n"
                                "Retry-After: 1\r\nConnection: close\r\n\r\n%s\n",
                                reason);
    if (length <= 0 || (size_t)length >= sizeof(response)) {
        return send_and_close(client, not_found_response, sizeof(not_found_response) - 1u);
    }
    return send_and_close(client, response, (size_t)length);
}

static err_t send_capture(http_client_t *client)
{
    if (stream_client != NULL) {
        return send_capture_unavailable(client, "MJPEG stream owns the camera");
    }
    const uint8_t *frame = NULL;
    uint32_t frame_length = 0u;
    if (!t384_camera_acquire_frame(&frame, &frame_length)) {
        if (t384_camera_trigger()) {
            return send_capture_unavailable(client, "capture started; retry shortly");
        }
        return send_capture_unavailable(client, "camera busy or not initialized");
    }

    const int header_length = snprintf(client->header, sizeof(client->header),
                                       "HTTP/1.0 200 OK\r\n"
                                       "Content-Type: image/jpeg\r\n"
                                       "Content-Length: %lu\r\n"
                                       "Cache-Control: no-store\r\n"
                                       "Connection: close\r\n\r\n",
                                       (unsigned long)frame_length);
    if (header_length <= 0 || (size_t)header_length >= sizeof(client->header)) {
        t384_camera_release_frame();
        return send_capture_unavailable(client, "response header error");
    }

    client->image_response = true;
    client->header_length = (size_t)header_length;
    client->header_offset = 0u;
    client->frame = frame;
    client->frame_length = frame_length;
    client->frame_offset = 0u;
    client->image_inflight = 0u;
    return queue_image_chunk(client);
}

static err_t send_stream(http_client_t *client)
{
    if (stream_client != NULL) {
        return send_capture_unavailable(client, "only one MJPEG stream is supported");
    }

    static const char response_header[] =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=t384frame\r\n"
        "Cache-Control: no-store\r\n"
        "Pragma: no-cache\r\n"
        "Connection: close\r\n\r\n";
    memcpy(client->header, response_header, sizeof(response_header) - 1u);
    client->header_length = sizeof(response_header) - 1u;
    client->header_offset = 0u;
    client->stream_response = true;
    client->stream_phase = STREAM_PHASE_HTTP_HEADER;
    client->idle_polls = 0u;
    stream_client = client;
    ++stream_stats.connects;
    stream_fps_started_ms = t384_millis();
    client->stream_last_progress_ms = stream_fps_started_ms;
    stream_fps_frames = 0u;
    stream_stats.fps_x1000 = 0u;
    return queue_stream_data(client);
}

static bool request_matches_path(const char *request, u16_t copied,
                                 const char *path)
{
    static const char method[] = "GET ";
    const size_t path_length = strlen(path);
    const size_t delimiter = sizeof(method) - 1u + path_length;
    return copied > delimiter &&
           memcmp(request, method, sizeof(method) - 1u) == 0 &&
           memcmp(request + sizeof(method) - 1u, path, path_length) == 0 &&
           (request[delimiter] == ' ' || request[delimiter] == '?');
}

static err_t http_receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t error)
{
    http_client_t *client = (http_client_t *)arg;
    if (p == NULL) {
        return close_client(client);
    }
    if (error != ERR_OK) {
        pbuf_free(p);
        abort_client(client);
        return ERR_ABRT;
    }

    client->idle_polls = 0u;

    char request[32] = {0};
    const u16_t copied = pbuf_copy_partial(p, request, sizeof(request) - 1u, 0u);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (request_matches_path(request, copied, "/diag")) {
        const size_t length = build_diag_response();
        if (length == 0u) {
            return send_and_close(client, not_found_response,
                                  sizeof(not_found_response) - 1u);
        }
        return send_keep_alive(client, diag_response, length);
    }
    if (request_matches_path(request, copied, "/capture.jpg")) {
        return send_capture(client);
    }
    if (request_matches_path(request, copied, "/stream.mjpg")) {
        return send_stream(client);
    }
    if (request_matches_path(request, copied, "/")) {
        return send_static_response(client, status_response,
                                    sizeof(status_response) - 1u);
    }
    return send_and_close(client, not_found_response, sizeof(not_found_response) - 1u);
}

static err_t http_accept(void *arg, struct tcp_pcb *pcb, err_t error)
{
    (void)arg;
    if (error != ERR_OK) {
        return error;
    }

    http_client_t *client = NULL;
    for (unsigned i = 0; i < HTTP_CLIENTS; ++i) {
        if (clients[i].pcb == NULL) {
            client = &clients[i];
            break;
        }
    }
    if (client == NULL) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    memset(client, 0, sizeof(*client));
    client->pcb = pcb;
    tcp_arg(pcb, client);
    tcp_recv(pcb, http_receive);
    tcp_sent(pcb, http_sent);
    tcp_poll(pcb, http_poll, HTTP_POLL_INTERVAL);
    tcp_err(pcb, http_error);
    return ERR_OK;
}

err_t t384_http_status_init(void)
{
    memset(clients, 0, sizeof(clients));
    struct tcp_pcb *listener = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (listener == NULL) {
        return ERR_MEM;
    }

    err_t error = tcp_bind(listener, IP_ANY_TYPE, T384_HTTP_PORT);
    if (error != ERR_OK) {
        tcp_close(listener);
        return error;
    }

    struct tcp_pcb *listening = tcp_listen_with_backlog_and_err(listener, HTTP_CLIENTS,
                                                                &error);
    if (listening == NULL) {
        tcp_close(listener);
        return error;
    }
    http_listener = listening;
    tcp_accept(http_listener, http_accept);
    return ERR_OK;
}

void t384_http_status_task(void)
{
    const uint32_t now = t384_millis();
    const uint32_t elapsed = (uint32_t)(now - stream_fps_started_ms);
    if (stream_client != NULL && elapsed >= 1000u) {
        stream_stats.fps_x1000 =
            elapsed == 0u ? 0u : (stream_fps_frames * 1000000u) / elapsed;
        stream_fps_frames = 0u;
        stream_fps_started_ms = now;
    }
    http_client_t *client = stream_client;
    if (client != NULL && client->pcb != NULL && !client->closing) {
        if (client->stream_blocked &&
            client->stream_phase != STREAM_PHASE_IDLE &&
            (uint32_t)(now - client->stream_last_progress_ms) >=
                HTTP_STREAM_STALL_TIMEOUT_MS) {
            ++stream_stats.timeout_disconnects;
            abort_client(client);
            return;
        }
        (void)queue_stream_data(client);
    }
}

void t384_http_status_reset(void)
{
    for (unsigned i = 0; i < HTTP_CLIENTS; ++i) {
        abort_client(&clients[i]);
    }
}
