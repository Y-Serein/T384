#include "http_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dhserver.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "t384_frame_pipeline.h"
#include "t384_frame_source.h"
#include "t384_ncm.h"
#include "t384_product_config.h"
#include "t384_raw16.h"
#include "t384_raw16_wire.h"
#include "t384_time.h"

#define HTTP_CLIENTS 3u
#define HTTP_POLL_INTERVAL 4u
#define HTTP_IDLE_POLL_LIMIT 5u
#define HTTP_RAW16_WRITE_BUDGET 16u
#define HTTP_RAW16_STALL_TIMEOUT_MS 10000u
#define HTTP_RAW16_TARGET_BPS 7000000u

typedef struct {
    struct tcp_pcb *pcb;
    uint8_t idle_polls;
    bool closing;
    bool static_response;
    bool raw16_response;
    bool raw16_blocked;
    const char *static_data;
    size_t static_length;
    size_t static_offset;
    u16_t static_inflight;
    size_t header_length;
    size_t header_offset;
    uint32_t raw16_last_progress_ms;
    bool raw16_chunk_leased;
    bool raw16_synced;
    t384_frame_chunk_view_t raw16_chunk;
    size_t raw16_wire_header_offset;
    size_t raw16_payload_offset;
    uint8_t raw16_wire_header[T384_RAW16_WIRE_HEADER_BYTES];
    char header[512];
} http_client_t;

typedef struct {
    uint32_t connects;
    uint32_t disconnects;
    uint32_t frames;
    uint64_t bytes;
    uint32_t backpressure;
    uint32_t write_errors;
    uint32_t timeout_disconnects;
    uint32_t fps_x1000;
    uint32_t payload_bps;
} raw16_stream_stats_t;

static http_client_t clients[HTTP_CLIENTS];
static struct tcp_pcb *http_listener;
static http_client_t *raw16_client;
static raw16_stream_stats_t raw16_stats;
static uint32_t raw16_rate_started_ms;
static uint32_t raw16_rate_frames;
static uint32_t raw16_rate_bytes;
static uint32_t http_accept_rejects;
static char diag_response[4096];

static const char status_response[] =
#include "device_console_html.inc"
;

static const char not_found_response[] =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Connection: close\r\n\r\nnot found\n";

static const char busy_response[] =
    "HTTP/1.0 409 Conflict\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Retry-After: 1\r\n"
    "Connection: close\r\n\r\nonly one RAW16 stream is supported\n";

static unsigned active_client_count(void)
{
    unsigned count = 0u;
    for (unsigned i = 0u; i < HTTP_CLIENTS; ++i) {
        if (clients[i].pcb != NULL) {
            ++count;
        }
    }
    return count;
}

static void format_u64_decimal(uint64_t value, char output[21])
{
    char reversed[20];
    size_t length = 0u;
    do {
        reversed[length++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);

    for (size_t i = 0u; i < length; ++i) {
        output[i] = reversed[length - i - 1u];
    }
    output[length] = '\0';
}

static size_t build_diag_response(void)
{
    t384_ncm_stats_t ncm;
    dhserv_stats_t dhcp;
    t384_frame_source_stats_t source;
    t384_frame_pipeline_stats_t pipeline;
    char raw16_bytes[21];
    char source_bytes[21];
    char pipeline_bytes[21];
    t384_ncm_get_stats(&ncm);
    dhserv_get_stats(&dhcp);
    t384_frame_source_get_stats(&source);
    t384_frame_pipeline_get_stats(&pipeline);
    format_u64_decimal(raw16_stats.bytes, raw16_bytes);
    format_u64_decimal((uint64_t)source.frames * T384_RAW16_FRAME_BYTES,
                       source_bytes);
    format_u64_decimal(pipeline.bytes_committed, pipeline_bytes);

    const unsigned stream_active = raw16_client != NULL ? 1u : 0u;
    enum { DIAG_HEADER_RESERVE = 192 };
    const int body_length = snprintf(
        diag_response + DIAG_HEADER_RESERVE,
        sizeof(diag_response) - DIAG_HEADER_RESERVE,
        "pipeline=raw16-source-pipeline-v1\n"
        "source.kind=%s\n"
        "source.synthetic=%lu\n"
        "source.target_bps=%lu\n"
        "source.frames=%lu\n"
        "source.published_frames=%lu\n"
        "source.dropped_frames=%lu\n"
        "source.schedule_overruns=%lu\n"
        "source.fps_x1000=%lu\n"
        "source.bytes=%s\n"
        "pipeline.chunk_rows=%u\n"
        "pipeline.chunk_bytes=%u\n"
        "pipeline.slot_count=%u\n"
        "pipeline.capacity_bytes=%u\n"
        "pipeline.queued_chunks=%lu\n"
        "pipeline.high_water_chunks=%lu\n"
        "pipeline.frames_started=%lu\n"
        "pipeline.frames_completed=%lu\n"
        "pipeline.frames_aborted=%lu\n"
        "pipeline.chunks_committed=%lu\n"
        "pipeline.chunks_released=%lu\n"
        "pipeline.bytes_committed=%s\n"
        "pipeline.acquire_no_slot=%lu\n"
        "pipeline.protocol_errors=%lu\n"
        "pipeline.producer_active=%lu\n"
        "pipeline.producer_leased=%lu\n"
        "pipeline.consumer_leased=%lu\n"
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
        "http.active_clients=%u\n"
        "http.accept_rejects=%lu\n"
        "camera.initialized=%lu\n"
        "camera.init_attempts=1\n"
        "camera.init_ok=%lu\n"
        "camera.init_fail=%lu\n"
        "camera.sensor_mid=0x0000\n"
        "camera.sensor_pid=source-adapter\n"
        "camera.requests=%lu\n"
        "camera.starts=%lu\n"
        "camera.frame_starts=%lu\n"
        "camera.row_chunks=%lu\n"
        "camera.frame_done_irqs=0\n"
        "camera.stop_frame_irqs=0\n"
        "camera.frame_ends=%lu\n"
        "camera.fifo_overflows=0\n"
        "camera.frames=%lu\n"
        "camera.published_frames=%lu\n"
        "camera.bad_frames=0\n"
        "camera.overflows=%lu\n"
        "camera.timeouts=%lu\n"
        "camera.dropped_ready=0\n"
        "camera.dropped_no_slot=%lu\n"
        "camera.bytes=%s\n"
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
        "stream.bytes=%s\n"
        "stream.backpressure=%lu\n"
        "stream.write_errors=%lu\n"
        "stream.timeout_disconnects=%lu\n"
        "stream.fps_x1000=%lu\n"
        "raw16.active=%u\n"
        "raw16.width=%u\n"
        "raw16.height=%u\n"
        "raw16.frame_bytes=%lu\n"
        "raw16.little_endian=1\n"
        "raw16.target_bps=%lu\n"
        "raw16.connects=%lu\n"
        "raw16.disconnects=%lu\n"
        "raw16.frames=%lu\n"
        "raw16.bytes=%s\n"
        "raw16.backpressure=%lu\n"
        "raw16.write_errors=%lu\n"
        "raw16.timeout_disconnects=%lu\n"
        "raw16.fps_x1000=%lu\n"
        "raw16.payload_bps=%lu\n"
        "raw16.payload_mb_s_x1000=%lu\n"
        "raw16.wire_version=%u\n"
        "raw16.chunk_header_bytes=%u\n",
        t384_frame_source_name(),
        (unsigned long)source.synthetic,
        (unsigned long)source.target_bps,
        (unsigned long)source.frames,
        (unsigned long)source.published_frames,
        (unsigned long)source.dropped_frames,
        (unsigned long)source.schedule_overruns,
        (unsigned long)source.source_fps_x1000,
        source_bytes,
        T384_PIPELINE_CHUNK_ROWS,
        T384_PIPELINE_CHUNK_BYTES,
        T384_PIPELINE_SLOT_COUNT,
        T384_PIPELINE_CHUNK_BYTES * T384_PIPELINE_SLOT_COUNT,
        (unsigned long)pipeline.queued_chunks,
        (unsigned long)pipeline.high_water_chunks,
        (unsigned long)pipeline.frames_started,
        (unsigned long)pipeline.frames_completed,
        (unsigned long)pipeline.frames_aborted,
        (unsigned long)pipeline.chunks_committed,
        (unsigned long)pipeline.chunks_released,
        pipeline_bytes,
        (unsigned long)pipeline.acquire_no_slot,
        (unsigned long)pipeline.protocol_errors,
        (unsigned long)pipeline.producer_active,
        (unsigned long)pipeline.producer_leased,
        (unsigned long)pipeline.consumer_leased,
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
        active_client_count(),
        (unsigned long)http_accept_rejects,
        (unsigned long)source.initialized,
        (unsigned long)source.initialized,
        (unsigned long)(source.initialized == 0u ? 1u : 0u),
        (unsigned long)source.frames,
        (unsigned long)source.frames,
        (unsigned long)source.frames,
        (unsigned long)pipeline.chunks_committed,
        (unsigned long)source.frames,
        (unsigned long)source.frames,
        (unsigned long)source.published_frames,
        (unsigned long)pipeline.acquire_no_slot,
        (unsigned long)source.schedule_overruns,
        (unsigned long)source.dropped_frames,
        source_bytes,
        (unsigned long)T384_RAW16_FRAME_BYTES,
        (unsigned long)T384_RAW16_FRAME_BYTES,
        (unsigned long)source.source_fps_x1000,
        (unsigned long)pipeline.producer_active,
        (unsigned long)(pipeline.queued_chunks != 0u ? 1u : 0u),
        (unsigned long)pipeline.consumer_leased,
        stream_active,
        (unsigned long)raw16_stats.connects,
        (unsigned long)raw16_stats.disconnects,
        (unsigned long)raw16_stats.frames,
        raw16_bytes,
        (unsigned long)raw16_stats.backpressure,
        (unsigned long)raw16_stats.write_errors,
        (unsigned long)raw16_stats.timeout_disconnects,
        (unsigned long)raw16_stats.fps_x1000,
        stream_active,
        T384_RAW16_WIDTH,
        T384_RAW16_HEIGHT,
        (unsigned long)T384_RAW16_FRAME_BYTES,
        (unsigned long)HTTP_RAW16_TARGET_BPS,
        (unsigned long)raw16_stats.connects,
        (unsigned long)raw16_stats.disconnects,
        (unsigned long)raw16_stats.frames,
        raw16_bytes,
        (unsigned long)raw16_stats.backpressure,
        (unsigned long)raw16_stats.write_errors,
        (unsigned long)raw16_stats.timeout_disconnects,
        (unsigned long)raw16_stats.fps_x1000,
        (unsigned long)raw16_stats.payload_bps,
        (unsigned long)(raw16_stats.payload_bps / 1000u),
        T384_RAW16_WIRE_VERSION,
        T384_RAW16_WIRE_HEADER_BYTES);

    if (body_length < 0 ||
        (size_t)body_length >= sizeof(diag_response) - DIAG_HEADER_RESERVE) {
        return 0u;
    }

    char header[DIAG_HEADER_RESERVE];
    const int header_length = snprintf(
        header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n",
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
    if (client == NULL) {
        return;
    }
    if (client->raw16_chunk_leased) {
        t384_frame_pipeline_release();
        client->raw16_chunk_leased = false;
    }
    if (raw16_client == client) {
        raw16_client = NULL;
        ++raw16_stats.disconnects;
        raw16_stats.fps_x1000 = 0u;
        raw16_stats.payload_bps = 0u;
    }
    memset(client, 0, sizeof(*client));
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
    if (client == NULL || client->pcb == NULL) {
        return ERR_OK;
    }
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

static err_t send_static_response(http_client_t *client,
                                  const char *response, size_t length)
{
    client->static_response = true;
    client->static_data = response;
    client->static_length = length;
    client->static_offset = 0u;
    client->static_inflight = 0u;
    client->idle_polls = 0u;
    return queue_static_chunk(client);
}

static err_t send_and_close(http_client_t *client, const char *response,
                            size_t length)
{
    if (length > tcp_sndbuf(client->pcb) || length > 0xffffu) {
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
    client->closing = true;
    return close_client(client);
}

static bool lease_next_raw16_chunk(http_client_t *client)
{
    t384_frame_chunk_view_t chunk;
    while (t384_frame_pipeline_peek(&chunk)) {
        if (!client->raw16_synced &&
            (chunk.flags & T384_CHUNK_FLAG_FRAME_START) == 0u) {
            t384_frame_pipeline_release();
            continue;
        }
        client->raw16_synced = true;
        client->raw16_chunk = chunk;
        client->raw16_chunk_leased = true;
        client->raw16_wire_header_offset = 0u;
        client->raw16_payload_offset = 0u;
        t384_raw16_wire_encode(client->raw16_wire_header, &chunk);
        return true;
    }
    return false;
}

static err_t queue_raw16_data(http_client_t *client)
{
    bool wrote = false;
    for (unsigned write_count = 0u;
         write_count < HTTP_RAW16_WRITE_BUDGET; ++write_count) {
        const u16_t send_space = tcp_sndbuf(client->pcb);
        if (send_space == 0u) {
            if (!client->raw16_blocked) {
                ++raw16_stats.backpressure;
                client->raw16_blocked = true;
            }
            break;
        }

        if (client->header_offset >= client->header_length &&
            !client->raw16_chunk_leased &&
            !lease_next_raw16_chunk(client)) {
            break;
        }
        if (client->raw16_chunk_leased &&
            (client->raw16_chunk.data == NULL ||
             client->raw16_chunk.length == 0u ||
             client->raw16_chunk.length > T384_PIPELINE_CHUNK_BYTES ||
             client->raw16_payload_offset > client->raw16_chunk.length)) {
            ++raw16_stats.write_errors;
            abort_client(client);
            return ERR_ABRT;
        }

        const uint8_t *data;
        size_t chunk_size;
        bool payload = false;
        if (client->header_offset < client->header_length) {
            chunk_size = client->header_length - client->header_offset;
            data = (const uint8_t *)client->header + client->header_offset;
        } else if (client->raw16_wire_header_offset <
                   T384_RAW16_WIRE_HEADER_BYTES) {
            chunk_size = T384_RAW16_WIRE_HEADER_BYTES -
                         client->raw16_wire_header_offset;
            data = client->raw16_wire_header +
                   client->raw16_wire_header_offset;
        } else {
            chunk_size = client->raw16_chunk.length -
                         client->raw16_payload_offset;
            payload = true;
            data = client->raw16_chunk.data + client->raw16_payload_offset;
        }
        if (chunk_size > send_space) {
            chunk_size = send_space;
        }
        if (chunk_size > 0xffffu) {
            chunk_size = 0xffffu;
        }
        const u16_t chunk = (u16_t)chunk_size;

        const err_t write_error = tcp_write(client->pcb, data, chunk,
                                            TCP_WRITE_FLAG_COPY);
        if (write_error == ERR_MEM) {
            if (!client->raw16_blocked) {
                ++raw16_stats.backpressure;
                client->raw16_blocked = true;
            }
            break;
        }
        if (write_error != ERR_OK) {
            ++raw16_stats.write_errors;
            abort_client(client);
            return ERR_ABRT;
        }

        wrote = true;
        client->raw16_blocked = false;
        client->raw16_last_progress_ms = t384_millis();
        if (payload) {
            client->raw16_payload_offset += chunk;
            raw16_stats.bytes += chunk;
            raw16_rate_bytes += chunk;
            if (client->raw16_payload_offset == client->raw16_chunk.length) {
                const bool frame_end =
                    (client->raw16_chunk.flags & T384_CHUNK_FLAG_FRAME_END) != 0u;
                t384_frame_pipeline_release();
                client->raw16_chunk_leased = false;
                client->raw16_payload_offset = 0u;
                if (frame_end) {
                    ++raw16_stats.frames;
                    ++raw16_rate_frames;
                }
            }
        } else if (client->header_offset < client->header_length) {
            client->header_offset += chunk;
        } else {
            client->raw16_wire_header_offset += chunk;
        }
    }

    if (wrote) {
        const err_t output_error = tcp_output(client->pcb);
        if (output_error != ERR_OK && output_error != ERR_MEM) {
            ++raw16_stats.write_errors;
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
            client->static_inflight =
                (u16_t)(client->static_inflight - length);
            return ERR_OK;
        }
        client->static_inflight = 0u;
        return queue_static_chunk(client);
    }
    if (client->closing) {
        return close_client(client);
    }
    if (client->raw16_response) {
        client->raw16_last_progress_ms = t384_millis();
        return queue_raw16_data(client);
    }
    return ERR_OK;
}

static err_t http_poll(void *arg, struct tcp_pcb *pcb)
{
    (void)pcb;
    http_client_t *client = (http_client_t *)arg;
    if (client->closing) {
        return close_client(client);
    }
    if (client->raw16_response) {
        return queue_raw16_data(client);
    }
    if (client->static_response && client->static_inflight == 0u) {
        const err_t result = queue_static_chunk(client);
        if (result != ERR_OK || client->pcb == NULL || client->closing) {
            return result;
        }
    }
    if (++client->idle_polls >= HTTP_IDLE_POLL_LIMIT) {
        abort_client(client);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t send_raw16_stream(http_client_t *client)
{
    if (raw16_client != NULL) {
        return send_and_close(client, busy_response,
                              sizeof(busy_response) - 1u);
    }

    const int header_length = snprintf(
        client->header, sizeof(client->header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/x-t384-raw16-chunks\r\n"
        "Cache-Control: no-store\r\n"
        "Pragma: no-cache\r\n"
        "Connection: close\r\n"
        "X-T384-Format: RAW16LE-CHUNK-V1\r\n"
        "X-T384-Wire-Version: %u\r\n"
        "X-T384-Chunk-Header-Bytes: %u\r\n"
        "X-T384-Chunk-Payload-Max: %u\r\n"
        "X-T384-Frame-Width: %u\r\n"
        "X-T384-Frame-Height: %u\r\n"
        "X-T384-Frame-Bytes: %lu\r\n\r\n",
        T384_RAW16_WIRE_VERSION, T384_RAW16_WIRE_HEADER_BYTES,
        T384_PIPELINE_CHUNK_BYTES, T384_RAW16_WIDTH, T384_RAW16_HEIGHT,
        (unsigned long)T384_RAW16_FRAME_BYTES);
    if (header_length <= 0 ||
        (size_t)header_length >= sizeof(client->header)) {
        return send_and_close(client, not_found_response,
                              sizeof(not_found_response) - 1u);
    }

    client->header_length = (size_t)header_length;
    client->header_offset = 0u;
    client->raw16_response = true;
    client->raw16_chunk_leased = false;
    client->raw16_synced = false;
    client->raw16_wire_header_offset = 0u;
    client->raw16_payload_offset = 0u;
    client->raw16_last_progress_ms = t384_millis();
    raw16_client = client;
    ++raw16_stats.connects;
    raw16_rate_started_ms = client->raw16_last_progress_ms;
    raw16_rate_frames = 0u;
    raw16_rate_bytes = 0u;
    raw16_stats.fps_x1000 = 0u;
    raw16_stats.payload_bps = 0u;
    return queue_raw16_data(client);
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

static err_t http_receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p,
                          err_t error)
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
    if (client->raw16_response) {
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    char request[64] = {0};
    const u16_t copied = pbuf_copy_partial(p, request,
                                           sizeof(request) - 1u, 0u);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (request_matches_path(request, copied, "/diag")) {
        const size_t length = build_diag_response();
        if (length != 0u) {
            return send_and_close(client, diag_response, length);
        }
    } else if (request_matches_path(request, copied, "/raw16.stream")) {
        return send_raw16_stream(client);
    } else if (request_matches_path(request, copied, "/")) {
        return send_static_response(client, status_response,
                                    sizeof(status_response) - 1u);
    }
    return send_and_close(client, not_found_response,
                          sizeof(not_found_response) - 1u);
}

static err_t http_accept(void *arg, struct tcp_pcb *pcb, err_t error)
{
    (void)arg;
    if (error != ERR_OK) {
        return error;
    }

    http_client_t *client = NULL;
    for (unsigned i = 0u; i < HTTP_CLIENTS; ++i) {
        if (clients[i].pcb == NULL) {
            client = &clients[i];
            break;
        }
    }
    if (client == NULL) {
        ++http_accept_rejects;
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
    memset(&raw16_stats, 0, sizeof(raw16_stats));
    raw16_client = NULL;
    http_accept_rejects = 0u;

    struct tcp_pcb *listener = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (listener == NULL) {
        return ERR_MEM;
    }
    err_t error = tcp_bind(listener, IP_ANY_TYPE, T384_HTTP_PORT);
    if (error != ERR_OK) {
        tcp_close(listener);
        return error;
    }
    struct tcp_pcb *listening = tcp_listen_with_backlog_and_err(
        listener, HTTP_CLIENTS, &error);
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
    const uint32_t elapsed = (uint32_t)(now - raw16_rate_started_ms);
    if (raw16_client != NULL && elapsed >= 1000u) {
        raw16_stats.fps_x1000 =
            (uint32_t)(((uint64_t)raw16_rate_frames * 1000000u) / elapsed);
        raw16_stats.payload_bps =
            (uint32_t)(((uint64_t)raw16_rate_bytes * 1000u) / elapsed);
        raw16_rate_started_ms = now;
        raw16_rate_frames = 0u;
        raw16_rate_bytes = 0u;
    }

    http_client_t *client = raw16_client;
    if (client == NULL || client->pcb == NULL || client->closing) {
        return;
    }
    if (client->raw16_blocked &&
        (uint32_t)(now - client->raw16_last_progress_ms) >=
            HTTP_RAW16_STALL_TIMEOUT_MS) {
        ++raw16_stats.timeout_disconnects;
        abort_client(client);
        return;
    }
    (void)queue_raw16_data(client);
}

void t384_http_status_reset(void)
{
    for (unsigned i = 0u; i < HTTP_CLIENTS; ++i) {
        abort_client(&clients[i]);
    }
}
