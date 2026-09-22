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
#include "t384_dualcore.h"
#include "t384_mini2_protocol.h"
#include "t384_ncm.h"
#include "t384_product_config.h"
#include "t384_raw16.h"
#include "t384_raw16_roi.h"
#include "t384_raw16_wire.h"
#include "t384_time.h"
#include "t384_calibration_storage.h"
#include "t384_module_files.h"
#include "t384_module_files_http.h"
#include "t384_v5f_net_memory.h"

#if T384_NETWORK_ON_V5F
/* Two sockets are enough for the image page plus /diag and keep the V5F
 * network-side control state inside the dedicated DTCM window. */
#define HTTP_CLIENTS 2u
#define HTTP_REQUEST_BYTES 2432u
#define HTTP_DIAG_BYTES 8192u
#else
#define HTTP_CLIENTS 3u
#define HTTP_REQUEST_BYTES 4096u
#define HTTP_DIAG_BYTES 8192u
#endif
#define HTTP_POLL_INTERVAL 4u
#define HTTP_IDLE_POLL_LIMIT 5u
#define HTTP_RAW16_WRITE_BUDGET 16u
#define HTTP_STATIC_WRITE_BUDGET 16u
#define HTTP_RAW16_STALL_TIMEOUT_MS 5000u
#define HTTP_RAW16_TARGET_BPS 4915200u
#define HTTP_IDLE_PIPELINE_DRAIN_BUDGET 32u
/* Keep the proven COPY path enabled.  The experimental single-slot no-copy
 * path serialized the 640 stream behind one ACK and is disabled until an
 * outstanding-slot queue replaces it. */
#define T384_RAW16_NO_COPY 0

typedef struct {
    struct tcp_pcb *pcb;
    uint8_t idle_polls;
    bool closing;
    bool static_response;
    bool raw16_response;
    bool raw16_blocked;
    bool module_download;
    uint32_t module_download_started;
    const uint8_t *static_data;
    size_t static_length;
    size_t static_offset;
    u16_t static_inflight;
    size_t header_length;
    size_t header_offset;
    uint32_t raw16_last_progress_ms;
    bool raw16_chunk_leased;
    bool raw16_synced;
    bool raw16_payload_queued;
    t384_frame_chunk_view_t raw16_chunk;
    size_t raw16_wire_header_offset;
    size_t raw16_payload_offset;
    uint32_t raw16_copy_unacked;
    uint32_t raw16_nocopy_unacked;
    uint8_t raw16_wire_header[T384_RAW16_WIRE_HEADER_BYTES];
    char header[512];
    uint8_t request[HTTP_REQUEST_BYTES];
    size_t request_length;
} http_client_t;

typedef struct {
    uint32_t connects;
    uint32_t disconnects;
    uint32_t frames;
    uint64_t bytes;
    uint32_t backpressure;
    uint32_t sendbuf_stalls;
    uint32_t write_mem_stalls;
    uint32_t write_errors;
    uint32_t timeout_disconnects;
    uint32_t fps_x1000;
    uint32_t payload_bps;
} raw16_stream_stats_t;

static http_client_t clients[HTTP_CLIENTS] T384_NET_HTTP_STORAGE;
static struct tcp_pcb *http_listener;
static http_client_t *raw16_client;
static raw16_stream_stats_t raw16_stats T384_NET_HTTP_STORAGE;
static uint32_t raw16_rate_started_ms;
static uint32_t raw16_rate_frames;
static uint32_t raw16_rate_bytes;
static uint32_t http_accept_rejects;
/* Keep the complete diagnostic body inside one bounded response. The WCH
 * formatter must not hit its truncation boundary while expanding the many
 * numeric counters below. */
static char diag_response[HTTP_DIAG_BYTES + (T384_DUALCORE ? 256 : 0)]
    T384_NET_HTTP_STORAGE;
/* The storage API caps payloads at 2 KiB; leave 256 B for HTTP headers. */
static char calibration_response[T384_CAL_STORAGE_MAX_PAYLOAD + 256u]
    T384_NET_HTTP_STORAGE;

#if T384_NETWORK_ON_V5F
/* The full console is compressed for the V5F 128 KiB image window. The
 * response includes its HTTP header and is transparently decompressed by
 * browsers while the MCU keeps the source page out of RAM_CODE. */
static const uint8_t status_response[]
    __attribute__((section(".t384_http_rodata"), aligned(4))) = {
#include "device_console_http_gz.inc"
};
#else
static const char status_response[] =
#include "device_console_html.inc"
;
#endif

static const char not_found_response[] =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Connection: close\r\n\r\nnot found\n";

static const char busy_response[] =
    "HTTP/1.0 409 Conflict\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Retry-After: 1\r\n"
    "Connection: close\r\n\r\nonly one RAW16 stream is supported\n";

static const char source_not_ready_response[] =
    "HTTP/1.0 503 Service Unavailable\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Retry-After: 1\r\n"
    "Connection: close\r\n\r\n"
    "MINI2 mode control or DVP stream is not ready; inspect /diag\n";

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

static void format_hex_bytes(const uint8_t *bytes, size_t length,
                             char *output)
{
    static const char hex[] = "0123456789ABCDEF";
    if (output == NULL) {
        return;
    }
    if (bytes == NULL) {
        output[0] = '\0';
        return;
    }
    for (size_t i = 0u; i < length; ++i) {
        output[i * 2u] = hex[bytes[i] >> 4];
        output[i * 2u + 1u] = hex[bytes[i] & 0x0Fu];
    }
    output[length * 2u] = '\0';
}

static size_t build_diag_response(void)
{
    t384_ncm_stats_t ncm;
    dhserv_stats_t dhcp;
    t384_frame_source_stats_t source;
    t384_frame_pipeline_stats_t pipeline;
    t384_raw16_roi_snapshot_t roi_snapshot;
    t384_raw16_roi_metrics_t roi_metrics;
    char raw16_bytes[21];
    char source_bytes[21];
    char pipeline_bytes[21];
    char first_row_prefix_hex[T384_FRAME_SOURCE_PREFIX_BYTES * 2u + 1u];
    t384_ncm_get_stats(&ncm);
    dhserv_get_stats(&dhcp);
    t384_frame_source_get_stats(&source);
    t384_frame_pipeline_get_stats(&pipeline);
    roi_snapshot.valid = source.roi_valid;
    roi_snapshot.frame_sequence = source.roi_frame_sequence;
    roi_snapshot.pipeline_published = source.roi_pipeline_published;
    roi_snapshot.sample_count = source.roi_sample_count;
    roi_snapshot.sum = source.roi_sum;
    roi_snapshot.sum_squares = source.roi_sum_squares;
    roi_snapshot.minimum = (uint16_t)source.roi_minimum;
    roi_snapshot.maximum = (uint16_t)source.roi_maximum;
    roi_snapshot.le_sum = source.roi_le_sum;
    roi_snapshot.le_sum_squares = source.roi_le_sum_squares;
    roi_snapshot.le_minimum = (uint16_t)source.roi_le_minimum;
    roi_snapshot.le_maximum = (uint16_t)source.roi_le_maximum;
    t384_raw16_roi_calculate(&roi_snapshot, &roi_metrics);
    format_u64_decimal(raw16_stats.bytes, raw16_bytes);
    const uint64_t observed_source_bytes = source.dvp_observed_bytes != 0u
                                               ? source.dvp_observed_bytes
                                               : (uint64_t)source.frames *
                                                     T384_RAW16_FRAME_BYTES;
    format_u64_decimal(observed_source_bytes, source_bytes);
    format_u64_decimal(pipeline.bytes_committed, pipeline_bytes);
    const size_t first_row_prefix_bytes =
        source.dvp_first_row_prefix_bytes <= T384_FRAME_SOURCE_PREFIX_BYTES
            ? source.dvp_first_row_prefix_bytes
            : 0u;
    format_hex_bytes(source.dvp_first_row_prefix, first_row_prefix_bytes,
                     first_row_prefix_hex);

    const unsigned stream_active = raw16_client != NULL ? 1u : 0u;
    struct tcp_pcb *stream_pcb = raw16_client != NULL ? raw16_client->pcb : NULL;
    const unsigned long tcp_sndbuf_value = stream_pcb != NULL
        ? (unsigned long)tcp_sndbuf(stream_pcb) : 0u;
    const unsigned long tcp_snd_wnd_value = stream_pcb != NULL
        ? (unsigned long)stream_pcb->snd_wnd : 0u;
    const unsigned long tcp_cwnd_value = stream_pcb != NULL
        ? (unsigned long)stream_pcb->cwnd : 0u;
    const unsigned long tcp_snd_queuelen_value = stream_pcb != NULL
        ? (unsigned long)stream_pcb->snd_queuelen : 0u;
    const unsigned long tcp_unacked_value = stream_pcb != NULL &&
        stream_pcb->unacked != NULL ? 1u : 0u;
    enum { DIAG_HEADER_RESERVE = 128 };
    int body_length = 0;
#define APPEND_DIAG(...) do {                                                   \
        const size_t used = (size_t)body_length;                                \
        const size_t available = sizeof(diag_response) -                        \
                                 DIAG_HEADER_RESERVE - used;                     \
        const int appended = snprintf(diag_response + DIAG_HEADER_RESERVE +     \
                                      used, available, __VA_ARGS__);             \
        if (appended < 0 || (size_t)appended >= available) return 0u;            \
        body_length += appended;                                                 \
    } while (0)

    APPEND_DIAG(
        "pipeline=raw16-source-pipeline-v1\n"
        "network.data_plane=%s\n"
        "source.kind=%s\n"
        "source.synthetic=%lu\n"
        "source.target_bps=%lu\n"
        "source.frames=%lu\n"
        "source.published_frames=%lu\n"
        "source.dropped_frames=%lu\n"
        "source.schedule_overruns=%lu\n"
        "source.fps_x1000=%lu\n"
        "source.stream_ready=%lu\n"
        "source.frame_mode=%lu\n"
        "source.pixel_format=%lu\n"
        "source.bytes=%s\n"
        "mini2.control_attempts=%lu\n"
        "mini2.control_tx_bytes=%lu\n"
        "mini2.control_ack_valid=%lu\n"
        "mini2.control_ack_status=%lu\n"
        "mini2.control_ack_timeout=%lu\n"
        "mini2.control_ack_bad=%lu\n"
        "mini2.control_digital_off_status=%lu\n"
        "mini2.control_analog_off_status=%lu\n"
        "mini2.detector_target_fps=%u\n"
        "mini2.dvp_target_fps=%u\n"
        "mini2.control_skipped_status=%u\n"
        "mini2.digital_query_bytes=%u\n"
        "mini2.control_detector30_status=%lu\n"
        "mini2.control_dvp30_status=%lu\n"
        "mini2.control_tpd_set_status=%lu\n"
        "mini2.control_tpd_query_status=%lu\n"
        "mini2.control_tpd_query_valid=%lu\n"
        "mini2.control_tpd_query_mode=%lu\n"
        "mini2.control_picture_fallback_used=%lu\n"
        "mini2.control_picture_set_status=%lu\n"
        "mini2.control_picture_dvp_status=%lu\n"
        "mini2.control_picture_query_status=%lu\n"
        "mini2.control_picture_query_valid=%lu\n"
        "mini2.control_picture_query_mode=%lu\n"
        "mini2.query_detector_valid=%lu\n"
        "mini2.query_detector_status=%lu\n"
        "mini2.query_detector_fps=%lu\n"
        "mini2.query_digital_valid=%lu\n"
        "mini2.query_digital_status=%lu\n"
        "mini2.query_digital_enabled=%lu\n"
        "mini2.query_digital_format=%lu\n"
        "mini2.query_digital_fps=%lu\n"
        "mini2.query_stream_mode_valid=%lu\n"
        "mini2.query_stream_mode_status=%lu\n"
        "mini2.query_stream_mode_0x85=%lu\n"
        "mini2.query_yuv_valid=%lu\n"
        "mini2.query_yuv_status=%lu\n"
        "mini2.query_yuv_format=%lu\n"
        "source.picture_pack_rejected_blocks=%lu\n"
        "mini2.query_auto_ffc_valid=%lu\n"
        "mini2.query_auto_ffc_status=%lu\n"
        "mini2.query_auto_ffc_enabled=%lu\n"
        "mini2.query_module_temp_valid=%lu\n"
        "mini2.query_module_temp_status=%lu\n"
        "mini2.query_module_temp_c_x100=%lu\n"
        "mini2.query_vtemp_valid=%lu\n"
        "mini2.query_vtemp_status=%lu\n"
        "mini2.query_vtemp_raw=%lu\n"
        "mini2.query_uptime_valid=%lu\n"
        "mini2.query_uptime_status=%lu\n"
        "mini2.query_uptime_seconds=%lu\n"
        "mini2.device_name_valid=%lu\n"
        "mini2.device_name=%s\n"
        "mini2.firmware_version_valid=%lu\n"
        "mini2.firmware_version=%s\n"
        "mini2.pn_valid=%lu\n"
        "mini2.pn=%s\n"
        "mini2.sn_valid=%lu\n"
        "mini2.sn=%s\n",
        T384_NETWORK_ON_V5F ? "v5f" : "v3f",
        t384_frame_source_name(),
        (unsigned long)source.synthetic,
        (unsigned long)source.target_bps,
        (unsigned long)source.frames,
        (unsigned long)source.published_frames,
        (unsigned long)source.dropped_frames,
        (unsigned long)source.schedule_overruns,
        (unsigned long)source.source_fps_x1000,
        (unsigned long)source.stream_ready,
        (unsigned long)source.frame_mode,
        (unsigned long)source.pixel_format,
        source_bytes,
        (unsigned long)source.mini2_control_attempts,
        (unsigned long)source.mini2_control_tx_bytes,
        (unsigned long)source.mini2_control_ack_valid,
        (unsigned long)source.mini2_control_ack_status,
        (unsigned long)source.mini2_control_ack_timeout,
        (unsigned long)source.mini2_control_ack_bad,
        (unsigned long)source.mini2_control_digital_off_status,
        (unsigned long)source.mini2_control_analog_off_status,
        T384_MINI2_DETECTOR_FPS,
        T384_MINI2_DVP_FPS,
        T384_MINI2_CONTROL_SKIPPED,
        T384_MINI2_DIGITAL_STATE_BYTES,
        (unsigned long)source.mini2_control_detector30_status,
        (unsigned long)source.mini2_control_dvp30_status,
        (unsigned long)source.mini2_control_tpd_set_status,
        (unsigned long)source.mini2_control_tpd_query_status,
        (unsigned long)source.mini2_control_tpd_query_valid,
        (unsigned long)source.mini2_control_tpd_query_mode,
        (unsigned long)source.mini2_control_picture_fallback_used,
        (unsigned long)source.mini2_control_picture_set_status,
        (unsigned long)source.mini2_control_picture_dvp_status,
        (unsigned long)source.mini2_control_picture_query_status,
        (unsigned long)source.mini2_control_picture_query_valid,
        (unsigned long)source.mini2_control_picture_query_mode,
        (unsigned long)source.mini2_query_detector_valid,
        (unsigned long)source.mini2_query_detector_status,
        (unsigned long)source.mini2_query_detector_fps,
        (unsigned long)source.mini2_query_digital_valid,
        (unsigned long)source.mini2_query_digital_status,
        (unsigned long)source.mini2_query_digital_enabled,
        (unsigned long)source.mini2_query_digital_format,
        (unsigned long)source.mini2_query_digital_fps,
        (unsigned long)source.mini2_query_stream_mode_valid,
        (unsigned long)source.mini2_query_stream_mode_status,
        (unsigned long)source.mini2_query_stream_mode_0x85,
        (unsigned long)source.mini2_query_yuv_valid,
        (unsigned long)source.mini2_query_yuv_status,
        (unsigned long)source.mini2_query_yuv_format,
        (unsigned long)source.picture_pack_rejected_blocks,
        (unsigned long)source.mini2_query_auto_ffc_valid,
        (unsigned long)source.mini2_query_auto_ffc_status,
        (unsigned long)source.mini2_query_auto_ffc_enabled,
        (unsigned long)source.mini2_query_module_temp_valid,
        (unsigned long)source.mini2_query_module_temp_status,
        (unsigned long)source.mini2_query_module_temp_c_x100,
        (unsigned long)source.mini2_query_vtemp_valid,
        (unsigned long)source.mini2_query_vtemp_status,
        (unsigned long)source.mini2_query_vtemp_raw,
        (unsigned long)source.mini2_query_uptime_valid,
        (unsigned long)source.mini2_query_uptime_status,
        (unsigned long)source.mini2_query_uptime_seconds,
        (unsigned long)source.mini2_device_name_valid,
        source.mini2_device_name,
        (unsigned long)source.mini2_firmware_version_valid,
        source.mini2_firmware_version,
        (unsigned long)source.mini2_pn_valid,
        source.mini2_pn,
        (unsigned long)source.mini2_sn_valid,
        source.mini2_sn);

    APPEND_DIAG(
        "dvp.timing_validated=%u\n"
        "dvp.config_pclk_falling=%u\n"
        "dvp.config_hsync_low=%u\n"
        "dvp.config_vsync_high=%u\n"
        "dvp.expected_row_bytes=%u\n"
        "dvp.expected_rows=%u\n"
        "dvp.expected_width=%u\n"
        "dvp.expected_height=%u\n"
        "dvp.expected_fps=%u\n"
        "dvp.dma_block_rows=%u\n"
        "dvp.dma_block_bytes=%u\n"
        "dvp.frame_starts=%lu\n"
        "dvp.row_events=%lu\n"
        "dvp.frame_done_irqs=%lu\n"
        "dvp.stop_frame_irqs=%lu\n"
        "dvp.frame_ends=%lu\n"
        "dvp.fifo_overflows=%lu\n"
        "dvp.orphan_rows=%lu\n"
        "dvp.bad_frames=%lu\n"
        "dvp.restarts=%lu\n"
        "dvp.restart_cr0=%lu\n"
        "dvp.restart_cr1=%lu\n"
        "dvp.restart_ifr=%lu\n"
        "dvp.module_probes=%lu\n"
        "dvp.module_rearms=%lu\n"
        "dvp.last_frame_rows=%lu\n"
        "dvp.last_frame_bytes=%lu\n"
        "dvp.observed_bytes=%s\n"
        "dvp.first_row_prefix_valid=%lu\n"
        "dvp.first_row_prefix_frame_sequence=%lu\n"
        "dvp.first_row_prefix_bytes=%lu\n"
        "dvp.first_row_prefix_hex=%s\n"
        "dvp.capture_active=%lu\n"
        "dvp.y16_input=%s\n"
        "roi.encoding=Y16BE\n"
        "roi.start_x=%u\n"
        "roi.start_y=%u\n"
        "roi.width=%u\n"
        "roi.height=%u\n"
        "roi.valid=%lu\n"
        "roi.frame_sequence=%lu\n"
        "roi.pipeline_published=%lu\n"
        "roi.sample_count=%lu\n"
        "roi.mean_raw_x100=%lu\n"
        "roi.stddev_raw_x100=%lu\n"
        "roi.minimum_raw=%lu\n"
        "roi.maximum_raw=%lu\n"
        "roi.be_mean_raw_x100=%lu\n"
        "roi.be_stddev_raw_x100=%lu\n"
        "roi.be_minimum_raw=%lu\n"
        "roi.be_maximum_raw=%lu\n"
        "roi.le_mean_raw_x100=%lu\n"
        "roi.le_stddev_raw_x100=%lu\n"
        "roi.le_minimum_raw=%lu\n"
        "roi.le_maximum_raw=%lu\n",
        T384_MINI2_DVP_TIMING_VALIDATED,
        T384_MINI2_DVP_PCLK_FALLING,
        T384_MINI2_DVP_HSYNC_LOW,
        T384_MINI2_DVP_VSYNC_HIGH,
        T384_MINI2_DVP_ROW_BYTES,
        T384_MINI2_DVP_EXPECTED_ROWS,
        T384_MINI2_DVP_WIDTH,
        T384_MINI2_DVP_HEIGHT,
        T384_MINI2_DVP_FPS,
        T384_MINI2_DMA_BLOCK_ROWS,
        T384_MINI2_DMA_BLOCK_BYTES,
        (unsigned long)source.dvp_frame_starts,
        (unsigned long)source.dvp_row_events,
        (unsigned long)source.dvp_frame_done_irqs,
        (unsigned long)source.dvp_stop_frame_irqs,
        (unsigned long)source.dvp_frame_ends,
        (unsigned long)source.dvp_fifo_overflows,
        (unsigned long)source.dvp_orphan_rows,
        (unsigned long)source.dvp_bad_frames,
        (unsigned long)source.dvp_restarts,
        (unsigned long)source.dvp_restart_cr0,
        (unsigned long)source.dvp_restart_cr1,
        (unsigned long)source.dvp_restart_ifr,
        (unsigned long)source.dvp_module_probes,
        (unsigned long)source.dvp_module_rearms,
        (unsigned long)source.dvp_last_frame_rows,
        (unsigned long)source.dvp_last_frame_bytes,
        source_bytes,
        (unsigned long)source.dvp_first_row_prefix_valid,
        (unsigned long)source.dvp_first_row_prefix_frame_sequence,
        (unsigned long)first_row_prefix_bytes,
        first_row_prefix_hex,
        (unsigned long)source.capture_active,
        source.frame_mode == T384_FRAME_MODE_TPD_Y16
            ? (T384_MINI2_DVP_Y16_LITTLE_ENDIAN ? "LE" : "BE")
            : source.frame_mode == T384_FRAME_MODE_PICTURE
                ? (source.mini2_query_yuv_valid != 0u
                    ? (source.mini2_query_yuv_format == 0u ? "UYVY"
                       : source.mini2_query_yuv_format == 1u ? "VYUY"
                       : source.mini2_query_yuv_format == 2u ? "YUYV"
                       : source.mini2_query_yuv_format == 3u ? "YVYU"
                       : "unknown")
                    : "UYVY")
                : "unknown",
        (T384_RAW16_WIDTH - T384_RAW16_ROI_WIDTH) / 2u,
        (T384_RAW16_HEIGHT - T384_RAW16_ROI_HEIGHT) / 2u,
        T384_RAW16_ROI_WIDTH,
        T384_RAW16_ROI_HEIGHT,
        (unsigned long)roi_snapshot.valid,
        (unsigned long)roi_snapshot.frame_sequence,
        (unsigned long)roi_snapshot.pipeline_published,
        (unsigned long)roi_snapshot.sample_count,
        (unsigned long)roi_metrics.mean_raw_x100,
        (unsigned long)roi_metrics.stddev_raw_x100,
        (unsigned long)roi_snapshot.minimum,
        (unsigned long)roi_snapshot.maximum,
        (unsigned long)roi_metrics.mean_raw_x100,
        (unsigned long)roi_metrics.stddev_raw_x100,
        (unsigned long)roi_snapshot.minimum,
        (unsigned long)roi_snapshot.maximum,
        (unsigned long)roi_metrics.le_mean_raw_x100,
        (unsigned long)roi_metrics.le_stddev_raw_x100,
        (unsigned long)roi_snapshot.le_minimum,
        (unsigned long)roi_snapshot.le_maximum);

    APPEND_DIAG(
        "pipeline.chunk_rows=%u\n"
        "pipeline.chunk_bytes=%u\n"
        "pipeline.slot_count=%u\n"
        "pipeline.capacity_bytes=%u\n"
        "pipeline.storage_capacity_bytes=%u\n"
        "pipeline.streaming=%u\n"
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
        "ncm.xmit_max_ntb_size=%lu\n"
        "ncm.xmit_max_datagrams=%lu\n"
        "ncm.xmit_free_ntb=%lu\n"
        "ncm.xmit_ready_ntb=%lu\n"
        "ncm.xmit_glue_active=%lu\n"
        "ncm.xmit_tinyusb_active=%lu\n"
        "ncm.xmit_glue_datagrams=%lu\n"
        "ncm.xmit_ntb_submit=%lu\n"
        "ncm.xmit_ntb_complete=%lu\n"
        "ncm.xmit_ntb_errors=%lu\n"
        "ncm.xmit_ntb_bytes=%lu\n"
        "ncm.xmit_ntb_datagrams=%lu\n"
        "ncm.xmit_ntb_1=%lu\n"
        "ncm.xmit_ntb_2_4=%lu\n"
        "ncm.xmit_ntb_5_8=%lu\n"
        "ncm.xmit_ntb_9_plus=%lu\n"
        "usb.recovery_guard=1\n"
        "ncm.mounts=%lu\n"
        "ncm.umounts=%lu\n"
        "ncm.suspends=%lu\n"
        "ncm.resumes=%lu\n",
        T384_PIPELINE_CHUNK_ROWS,
        T384_PIPELINE_CHUNK_BYTES,
        T384_PIPELINE_SLOT_COUNT,
        T384_PIPELINE_CHUNK_BYTES * T384_PIPELINE_SLOT_COUNT,
        T384_PIPELINE_STORAGE_CHUNK_BYTES * T384_PIPELINE_SLOT_COUNT,
        (unsigned)T384_PIPELINE_STREAMING,
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
        (unsigned long)ncm.xmit_max_ntb_size,
        (unsigned long)ncm.xmit_max_datagrams,
        (unsigned long)ncm.xmit_free_ntb,
        (unsigned long)ncm.xmit_ready_ntb,
        (unsigned long)ncm.xmit_glue_active,
        (unsigned long)ncm.xmit_tinyusb_active,
        (unsigned long)ncm.xmit_glue_datagrams,
        (unsigned long)ncm.xmit_ntb_submit,
        (unsigned long)ncm.xmit_ntb_complete,
        (unsigned long)ncm.xmit_ntb_errors,
        (unsigned long)ncm.xmit_ntb_bytes,
        (unsigned long)ncm.xmit_ntb_datagrams,
        (unsigned long)ncm.xmit_ntb_1,
        (unsigned long)ncm.xmit_ntb_2_4,
        (unsigned long)ncm.xmit_ntb_5_8,
        (unsigned long)ncm.xmit_ntb_9_plus,
        (unsigned long)ncm.mounts,
        (unsigned long)ncm.umounts,
        (unsigned long)ncm.suspends,
        (unsigned long)ncm.resumes);

    APPEND_DIAG(
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
        "http.close_callback_isolation=1\n"
        "http.static_window_refill=1\n"
        "http.checksum_aligned_reads=1\n"
        "http.tcp_send_buffer_bytes=%u\n"
        "tcp.sndbuf=%lu\n"
        "tcp.snd_wnd=%lu\n"
        "tcp.cwnd=%lu\n"
        "tcp.snd_queuelen=%lu\n"
        "tcp.unacked_present=%lu\n"
        "camera.initialized=%lu\n"
        "camera.sensor_pid=source-adapter\n"
        "camera.published_frames=%lu\n"
        "camera.bad_frames=%lu\n"
        "camera.overflows=%lu\n"
        "camera.timeouts=0\n"
        "camera.dropped_no_slot=%lu\n"
        "camera.last_frame_bytes=%lu\n"
        "camera.source_fps_x1000=%lu\n"
        "stream.active=%u\n"
        "stream.connects=%lu\n"
        "stream.disconnects=%lu\n"
        "stream.frames=%lu\n"
        "stream.bytes=%s\n"
        "stream.backpressure=%lu\n"
        "stream.sendbuf_stalls=%lu\n"
        "stream.write_mem_stalls=%lu\n"
        "stream.write_errors=%lu\n"
        "stream.timeout_disconnects=%lu\n"
        "stream.fps_x1000=%lu\n"
        "stream.payload_bps=%lu\n",
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
        (unsigned)TCP_SND_BUF,
        tcp_sndbuf_value,
        tcp_snd_wnd_value,
        tcp_cwnd_value,
        tcp_snd_queuelen_value,
        tcp_unacked_value,
        (unsigned long)source.initialized,
        (unsigned long)source.published_frames,
        (unsigned long)source.dvp_bad_frames,
        (unsigned long)source.dvp_fifo_overflows,
        (unsigned long)source.dropped_frames,
        (unsigned long)source.dvp_last_frame_bytes,
        (unsigned long)source.source_fps_x1000,
        stream_active,
        (unsigned long)raw16_stats.connects,
        (unsigned long)raw16_stats.disconnects,
        (unsigned long)raw16_stats.frames,
        raw16_bytes,
        (unsigned long)raw16_stats.backpressure,
        (unsigned long)raw16_stats.sendbuf_stalls,
        (unsigned long)raw16_stats.write_mem_stalls,
        (unsigned long)raw16_stats.write_errors,
        (unsigned long)raw16_stats.timeout_disconnects,
        (unsigned long)raw16_stats.fps_x1000,
        (unsigned long)raw16_stats.payload_bps);

#if T384_DUALCORE
    const t384_dualcore_shared_t *shared = &t384_dualcore_shared;
    APPEND_DIAG(
        "dualcore.v5f_booted=%lu\n"
        "dualcore.v5f_initialized=%lu\n"
        "dualcore.dtcm_access=%lu\n"
        "dualcore.frame_banks=%u\n"
        "dualcore.frame_state=%lu\n"
        "dualcore.frame_state1=%lu\n",
        (unsigned long)__atomic_load_n(&shared->v5f_booted, __ATOMIC_ACQUIRE),
        (unsigned long)__atomic_load_n(&shared->v5f_initialized, __ATOMIC_ACQUIRE),
        (unsigned long)__atomic_load_n(&shared->v3f_frame_access_ok, __ATOMIC_ACQUIRE),
#if T384_PIPELINE_STREAMING
        0u,
        (unsigned long)__atomic_load_n(&shared->ring.active, __ATOMIC_ACQUIRE),
        (unsigned long)__atomic_load_n(&shared->read_leased, __ATOMIC_ACQUIRE));
#else
        T384_FRAME_BANKS,
        (unsigned long)__atomic_load_n(&shared->banks[0].state, __ATOMIC_ACQUIRE),
        (unsigned long)__atomic_load_n(&shared->banks[1].state, __ATOMIC_ACQUIRE));
#endif
#endif
#undef APPEND_DIAG
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

/* A no-copy payload remains owned by the pipeline until the peer ACKs every
 * byte.  The force path is used only while tearing down a dead TCP PCB. */
static void release_raw16_chunk(http_client_t *client, bool force)
{
    if (client == NULL || !client->raw16_chunk_leased) return;
#if T384_RAW16_NO_COPY
    if (!force && (!client->raw16_payload_queued ||
                   client->raw16_copy_unacked != 0u ||
                   client->raw16_nocopy_unacked != 0u)) {
        return;
    }
#else
    (void)force;
#endif
    const bool frame_end =
        (client->raw16_chunk.flags & T384_CHUNK_FLAG_FRAME_END) != 0u;
    t384_frame_pipeline_release();
    client->raw16_chunk_leased = false;
    client->raw16_payload_queued = false;
    client->raw16_payload_offset = 0u;
    client->raw16_copy_unacked = 0u;
    client->raw16_nocopy_unacked = 0u;
    if (frame_end && !force) {
        ++raw16_stats.frames;
        ++raw16_rate_frames;
    }
}

#if T384_RAW16_NO_COPY
static void account_raw16_ack(http_client_t *client, u16_t length)
{
    uint32_t acked = length;
    if (client->raw16_copy_unacked >= acked) {
        client->raw16_copy_unacked -= acked;
        return;
    }
    acked -= client->raw16_copy_unacked;
    client->raw16_copy_unacked = 0u;
    if (client->raw16_nocopy_unacked >= acked) {
        client->raw16_nocopy_unacked -= acked;
    } else {
        client->raw16_nocopy_unacked = 0u;
    }
    release_raw16_chunk(client, false);
}
#endif

static void release_client(http_client_t *client)
{
    if (client == NULL) {
        return;
    }
    if (client->module_download) {
        const bool complete = client->static_offset == client->static_length &&
                              client->static_inflight == 0u;
        t384_module_files_download_release(complete);
    }
    release_raw16_chunk(client, true);
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
    if (client == NULL) {
        return;
    }
    if (client->pcb == NULL) {
        /* The PCB is already gone (for example after an asynchronous error);
         * there are no TCP pbufs left to drain, so finish the lease cleanup. */
        release_client(client);
        return;
    }
    struct tcp_pcb *pcb = client->pcb;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_poll(pcb, NULL, 0u);
    tcp_err(pcb, NULL);
    /* tcp_abort() frees unacked/unsent pbufs.  Keep a no-copy pipeline slot
     * leased until that happens; releasing it first would let the producer
     * overwrite memory still referenced by lwIP. */
    tcp_abort(pcb);
    release_client(client);
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
    if (client->raw16_response) {
        /* A RAW16 response may still have no-copy pbufs on either TCP queue;
         * graceful close would release the slot before their ACK/cleanup. */
        abort_client(client);
        return ERR_ABRT;
    }
    struct tcp_pcb *pcb = client->pcb;
    void *callback_arg = pcb->callback_arg;
    const tcp_recv_fn recv = pcb->recv;
    const tcp_sent_fn sent = pcb->sent;
    const tcp_poll_fn poll = pcb->poll;
    const tcp_err_fn err = pcb->errf;
    const u8_t poll_interval = pcb->pollinterval;
    /* A successful tcp_close can retain this PCB until FIN/ACK or timeout.
     * Detach before close (which may free it), before reusing the HTTP slot. */
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_poll(pcb, NULL, 0u);
    tcp_err(pcb, NULL);
    const err_t error = tcp_close(pcb);
    if (error == ERR_OK) {
        release_client(client);
        return ERR_OK;
    }
    if (error == ERR_MEM) {
        /* Failed close leaves the PCB owned by us; preserve retry callbacks. */
        tcp_arg(pcb, callback_arg);
        tcp_recv(pcb, recv);
        tcp_sent(pcb, sent);
        tcp_poll(pcb, poll, poll_interval);
        tcp_err(pcb, err);
        client->closing = true;
        return ERR_OK;
    }
    abort_client(client);
    return ERR_ABRT;
}

static err_t queue_static_chunk(http_client_t *client)
{
    if (client->static_offset >= client->static_length) {
        if (client->static_inflight != 0u) return ERR_OK;
        client->closing = true;
        return close_client(client);
    }

    bool wrote = false;
    for (unsigned writes = 0u; writes < HTTP_STATIC_WRITE_BUDGET &&
         client->static_offset < client->static_length; ++writes) {
        const size_t left = client->static_length - client->static_offset;
        const u16_t send_space = tcp_sndbuf(client->pcb);
        u16_t chunk = (u16_t)(left > TCP_MSS ? TCP_MSS : left);
        if (chunk > send_space) chunk = send_space;
        const u16_t inflight_space = (u16_t)(0xffffu - client->static_inflight);
        if (chunk > inflight_space) chunk = inflight_space;
        if (chunk == 0u) break;
        const err_t write_error = tcp_write(
            client->pcb, client->static_data + client->static_offset, chunk,
            TCP_WRITE_FLAG_COPY);
        if (write_error == ERR_MEM) break;
        if (write_error != ERR_OK) {
            abort_client(client);
            return ERR_ABRT;
        }
        client->static_offset += chunk;
        client->static_inflight = (u16_t)(client->static_inflight + chunk);
        wrote = true;
    }
    if (!wrote) return ERR_OK;
    const err_t output_error = tcp_output(client->pcb);
    if (output_error != ERR_OK && output_error != ERR_MEM) {
        abort_client(client);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t send_static_response(http_client_t *client,
                                  const void *response, size_t length)
{
    client->static_response = true;
    client->static_data = (const uint8_t *)response;
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
#if T384_PIPELINE_PACKED_PICTURE
        /* The ring already holds a lossless 32-byte UV prefix and 2560 Y
         * bytes per slot. V5F no-copy keeps the lease until tcp_sent ACK. */
        chunk.frame_offset = (chunk.frame_offset / T384_PIPELINE_CHUNK_BYTES) *
                             T384_PIPELINE_STORAGE_CHUNK_BYTES;
        chunk.length = T384_PIPELINE_STORAGE_CHUNK_BYTES;
        chunk.flags |= T384_CHUNK_FLAG_PICTURE_PACKED;
#endif
        client->raw16_chunk = chunk;
        client->raw16_chunk_leased = true;
        client->raw16_payload_queued = false;
        client->raw16_wire_header_offset = 0u;
        client->raw16_payload_offset = 0u;
        client->raw16_copy_unacked = 0u;
        client->raw16_nocopy_unacked = 0u;
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
                ++raw16_stats.sendbuf_stalls;
                client->raw16_blocked = true;
            }
            break;
        }

        if (client->header_offset >= client->header_length &&
            !client->raw16_chunk_leased &&
            !lease_next_raw16_chunk(client)) {
            break;
        }
#if T384_RAW16_NO_COPY
        /* The payload pointer remains owned by the pipeline until the ACK
         * callback drains raw16_nocopy_unacked.  Do not issue a zero-length
         * tcp_write while waiting for that callback, and do not advance to a
         * second slot before the current slot is released. */
        if (client->raw16_payload_queued) {
            break;
        }
#endif
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

        const uint8_t write_flags =
            payload && T384_RAW16_NO_COPY ? 0u : TCP_WRITE_FLAG_COPY;
        const err_t write_error = tcp_write(client->pcb, data, chunk,
                                            write_flags);
        if (write_error == ERR_MEM) {
            if (!client->raw16_blocked) {
                ++raw16_stats.backpressure;
                ++raw16_stats.write_mem_stalls;
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
#if T384_RAW16_NO_COPY
            client->raw16_nocopy_unacked += chunk;
#else
            /* COPY has already detached the source buffer from TCP. */
#endif
            client->raw16_payload_offset += chunk;
            raw16_stats.bytes += chunk;
            raw16_rate_bytes += chunk;
            if (client->raw16_payload_offset == client->raw16_chunk.length) {
#if T384_RAW16_NO_COPY
                client->raw16_payload_queued = true;
                release_raw16_chunk(client, false);
#else
                release_raw16_chunk(client, false);
#endif
            }
        } else if (client->header_offset < client->header_length) {
            client->header_offset += chunk;
            client->raw16_copy_unacked += chunk;
        } else {
            client->raw16_wire_header_offset += chunk;
            client->raw16_copy_unacked += chunk;
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
        } else {
            client->static_inflight = 0u;
        }
        return queue_static_chunk(client);
    }
    if (client->closing) {
        return close_client(client);
    }
    if (client->raw16_response) {
#if T384_RAW16_NO_COPY
        account_raw16_ack(client, length);
#endif
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

typedef struct {
    const char *model;
    unsigned long zero_c_x100;
    unsigned long counts_per_c_x100;
} t384_temp_model_config_t;

/* Expose only a complete, sane experimental model for this profile. OEM
 * readiness remains independent of this engineering display mapping. */
static void get_temp_model_config(t384_temp_model_config_t *cfg)
{
    t384_cal_manifest_t manifest;
    if (cfg == NULL) return;
    char profile[T384_CAL_STORAGE_PROFILE_MAX];
    snprintf(profile, sizeof(profile), "%ux%u", T384_RAW16_WIDTH, T384_RAW16_HEIGHT);
    cfg->model = T384_EXPERIMENTAL_TEMP_MODEL;
    cfg->zero_c_x100 = (unsigned long)T384_EXPERIMENTAL_Y16_ZERO_C_X100;
    cfg->counts_per_c_x100 = (unsigned long)T384_EXPERIMENTAL_Y16_COUNTS_PER_C_X100;
#if T384_RAW16_PROFILE == 640u
    /* Image-only bring-up: never inherit a saved model from the 384 test. */
    return;
#endif
    if (t384_cal_storage_manifest(&manifest) == T384_CAL_OK &&
        strcmp(manifest.profile, profile) == 0 &&
        manifest.payload_len == sizeof(t384_cal_empirical_2point_t)) {
        uint8_t buf[sizeof(t384_cal_empirical_2point_t)];
        if (t384_cal_storage_read_data(0u, buf, sizeof(buf)) == T384_CAL_OK) {
            t384_cal_empirical_2point_t model;
            memcpy(&model, buf, sizeof(model));
            if (model.hot_c <= model.zero_c || model.hot_raw <= model.zero_raw ||
                model.hot_raw > 65535u || model.counts_per_c_x1000 <= 0)
                return;
            const uint32_t counts = (uint32_t)(((int64_t)model.counts_per_c_x1000 + 5) / 10);
            const int64_t zero = (int64_t)model.zero_raw * 100 -
                                 (int64_t)model.zero_c * counts;
            if (counts == 0u || zero < 0 || zero > 6553500) return;
            /* manifest is a stack local; never return its model pointer. */
            cfg->model = T384_CAL_MODEL_EMPIRICAL_2POINT;
            cfg->zero_c_x100 = (unsigned long)zero;
            cfg->counts_per_c_x100 = (unsigned long)counts;
            return;
        }
    }
}

static err_t send_raw16_stream(http_client_t *client)
{
    if (t384_module_files_busy()) {
        return send_and_close(client, busy_response, sizeof(busy_response)-1u);
    }
    if (!t384_frame_source_stream_ready()) {
        return send_and_close(client, source_not_ready_response,
                              sizeof(source_not_ready_response) - 1u);
    }
    if (raw16_client != NULL) {
        return send_and_close(client, busy_response,
                              sizeof(busy_response) - 1u);
    }

    const uint16_t pixel_format = t384_frame_source_pixel_format();
    const bool y16 = pixel_format == T384_FRAME_PIXEL_FORMAT_Y16_BE;
    const char *frame_mode = y16 ? "tpd" : "picture-fallback";
    t384_temp_model_config_t temp_cfg;
    get_temp_model_config(&temp_cfg);
    const int header_length = snprintf(
        client->header, sizeof(client->header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/x-t384-frame-chunks\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "X-T384-Format: T384-FRAME-CHUNK-V%u\r\n"
        "X-T384-Frame-Mode: %s\r\n"
        "X-T384-Pixel-Format: %s\r\n"
        "X-T384-Pixel-Format-Code: %u\r\n"
        "X-T384-Temperature-Model: %s\r\n"
        "X-T384-Y16-Linear-X100: %lu,%lu\r\n"
        "X-T384-Wire-Version: %u\r\n"
        "X-T384-Chunk-Header-Bytes: %u\r\n"
        "X-T384-Chunk-Payload-Max: %u\r\n"
        "X-T384-Frame-Width: %u\r\n"
        "X-T384-Frame-Height: %u\r\n"
        "X-T384-Frame-Bytes: %lu\r\n\r\n",
        T384_RAW16_WIRE_VERSION, frame_mode,
        t384_frame_pixel_format_name(T384_PIPELINE_PACKED_PICTURE
            ? T384_FRAME_PIXEL_FORMAT_PACKED_UYVY : pixel_format),
        T384_PIPELINE_PACKED_PICTURE
            ? T384_FRAME_PIXEL_FORMAT_PACKED_UYVY : pixel_format,
        y16 ? temp_cfg.model : "unavailable",
        temp_cfg.zero_c_x100,
        temp_cfg.counts_per_c_x100,
        T384_RAW16_WIRE_VERSION, T384_RAW16_WIRE_HEADER_BYTES,
        T384_PIPELINE_STORAGE_CHUNK_BYTES, T384_RAW16_WIDTH, T384_RAW16_HEIGHT,
        (unsigned long)(T384_PIPELINE_PACKED_PICTURE
            ? T384_PIPELINE_PACKED_FRAME_BYTES : T384_RAW16_FRAME_BYTES));
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
    client->raw16_payload_queued = false;
    client->raw16_wire_header_offset = 0u;
    client->raw16_payload_offset = 0u;
    client->raw16_copy_unacked = 0u;
    client->raw16_nocopy_unacked = 0u;
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

static err_t send_json_status(http_client_t *client, int code, const char *reason,
                              const char *body)
{
    const int n = snprintf(calibration_response, sizeof(calibration_response),
                           "HTTP/1.0 %d %s\r\nContent-Type: application/json\r\n"
                           "Cache-Control: no-store\r\nConnection: close\r\n\r\n%s",
                           code, reason, body ? body : "{}");
    if (n <= 0 || (size_t)n >= sizeof(calibration_response)) {
        return send_and_close(client, not_found_response, sizeof(not_found_response)-1u);
    }
    return send_and_close(client, calibration_response, (size_t)n);
}

static err_t send_binary_status(http_client_t *client, int code, const char *reason,
                                const uint8_t *body, size_t length)
{
    const int n = snprintf(calibration_response, sizeof(calibration_response),
                           "HTTP/1.0 %d %s\r\nContent-Type: application/octet-stream\r\n"
                           "Cache-Control: no-store\r\nContent-Length: %lu\r\n"
                           "Connection: close\r\n\r\n",
                           code, reason, (unsigned long)length);
    if (n <= 0 || (size_t)n + length > sizeof(calibration_response)) {
        return send_json_status(client, 500, "Internal Server Error",
                                "{\"error\":\"response_too_large\"}");
    }
    if (length != 0u && body != NULL) memcpy(calibration_response + n, body, length);
    return send_and_close(client, calibration_response,
                          (size_t)n + length);
}

static t384_cal_status_t storage_put_packet(const uint8_t *data, size_t len)
{
    if (!data || len < sizeof(t384_cal_manifest_t)) return T384_CAL_FORMAT;
    t384_cal_manifest_t manifest;
    memcpy(&manifest, data, sizeof(manifest));
    if ((size_t)manifest.payload_len != len-sizeof(manifest)) return T384_CAL_FORMAT;
    if (t384_cal_crc32(data+sizeof(manifest),len-sizeof(manifest)) != manifest.payload_crc32)
        return T384_CAL_CRC;
    char profile[T384_CAL_STORAGE_PROFILE_MAX];
    snprintf(profile,sizeof(profile),"%ux%u",T384_RAW16_WIDTH,T384_RAW16_HEIGHT);
    if (!memchr(manifest.profile,0,sizeof(manifest.profile)) ||
        strcmp(manifest.profile,profile)!=0) return T384_CAL_FORMAT;
    const t384_cal_status_t begun=t384_cal_storage_begin(&manifest);
    if (begun != T384_CAL_OK) return begun;
    const uint32_t payload_len = manifest.payload_len;
    if ((size_t)payload_len != len - sizeof(manifest)
        || t384_cal_storage_write(0u, data + sizeof(manifest), payload_len)
               != T384_CAL_OK) {
        t384_cal_storage_abort();
        return T384_CAL_FORMAT;
    }
    return T384_CAL_OK;
}

static err_t handle_calibration_request(http_client_t *client)
{
    char *request = (char *)client->request;
    const size_t n = client->request_length;
    const char *end = strstr(request, "\r\n\r\n");
    if (end == NULL) return send_json_status(client, 400, "Bad Request", "{\"error\":\"headers\"}");
    const size_t header_len = (size_t)(end - request) + 4u;
    const char *body = request + header_len;
    size_t body_len = n >= header_len ? n - header_len : 0u;
    size_t content_length = 0u;
    bool has_content_length=false;
    const char *line=strstr(request,"\r\n");
    if (!line) return send_json_status(client,400,"Bad Request","{\"error\":\"headers\"}");
    for (line+=2; line<end; ) {
        const char *next=strstr(line,"\r\n");
        const char *colon=memchr(line,':',(size_t)(next-line));
        if (!colon || line[0]==' ' || line[0]=='\t')
            return send_json_status(client,400,"Bad Request","{\"error\":\"headers\"}");
        const size_t name_length=(size_t)(colon-line);
        char name[18];
        if (name_length<sizeof(name)) {
            for (size_t i=0;i<name_length;++i)
                name[i]=(line[i]>='A' && line[i]<='Z')?(char)(line[i]+('a'-'A')):line[i];
            name[name_length]=0;
            if (strcmp(name,"transfer-encoding")==0)
                return send_json_status(client,400,"Bad Request","{\"error\":\"transfer_encoding\"}");
            if (strcmp(name,"content-length")==0) {
                if (has_content_length)
                    return send_json_status(client,400,"Bad Request","{\"error\":\"duplicate_length\"}");
                has_content_length=true;
                const char *value=colon+1;
                while (value<next && (*value==' ' || *value=='\t')) ++value;
                if (value==next || *value<'0' || *value>'9')
                    return send_json_status(client,400,"Bad Request","{\"error\":\"content_length\"}");
                while (value<next && *value>='0' && *value<='9') {
                    const size_t digit=(size_t)(*value-'0');
                    if (content_length>(3072u-digit)/10u)
                        return send_json_status(client,413,"Payload Too Large","{\"error\":\"body_too_large\"}");
                    content_length=content_length*10u+digit; ++value;
                }
                while (value<next && (*value==' ' || *value=='\t')) ++value;
                if (value!=next)
                    return send_json_status(client,400,"Bad Request","{\"error\":\"content_length\"}");
            }
        }
        line=next+2;
    }
    if (body_len<content_length) return ERR_OK;
    if (body_len!=content_length)
        return send_json_status(client,400,"Bad Request","{\"error\":\"body_length\"}");
    const bool is_device = strncmp(request, "GET /api/v1/device ", sizeof("GET /api/v1/device ") - 1u) == 0;
    const bool is_manifest = strncmp(request, "GET /api/v1/calibration/v1/manifest ", sizeof("GET /api/v1/calibration/v1/manifest ") - 1u) == 0;
    const bool is_data = strncmp(request, "GET /api/v1/calibration/v1/data ", sizeof("GET /api/v1/calibration/v1/data ") - 1u) == 0;
    if (is_device) {
        const int encoded = snprintf((char *)client->request, sizeof(client->request),
            "{\"product\":\"T384\",\"api\":\"v1\",\"protocol_status\":\"legacy-chunk-stream\","
            "\"sensor\":{\"native_width\":%u,\"native_height\":%u},"
            "\"network\":{\"segment\":%u,\"device_ip\":\"192.168.%u.1\",\"configurable\":false,\"config_url\":\"/api/v1/network\"},"
            "\"ota\":{\"supported\":false}}",
            T384_RAW16_WIDTH, T384_RAW16_HEIGHT, T384_NCM_IPV4_C, T384_NCM_IPV4_C);
        if (encoded < 0 || (size_t)encoded >= sizeof(client->request))
            return send_json_status(client, 500, "Internal Server Error", "{}");
        return send_json_status(client, 200, "OK", (const char *)client->request);
    }
    if (is_manifest) {
        t384_cal_manifest_t manifest;
        const t384_cal_status_t status = t384_cal_storage_manifest(&manifest);
        if (status != T384_CAL_OK) return send_json_status(client, 404, "Not Found", "{\"error\":\"unavailable\"}");
        char identity[2u*T384_CAL_STORAGE_ID_MAX+1u];
        static const char hex[]="0123456789abcdef";
        for (size_t i=0;i<T384_CAL_STORAGE_ID_MAX;++i) {
            identity[2u*i]=hex[manifest.identity[i]>>4];
            identity[2u*i+1u]=hex[manifest.identity[i]&15u];
        }
        identity[sizeof(identity)-1u]=0;
        const int n = snprintf((char *)client->request, sizeof(client->request),
                               "{\"schema\":%lu,\"generation\":%lu,\"payload_len\":%lu,\"payload_crc32\":%lu,\"calibration_id\":%lu,\"model\":\"%s\",\"profile\":\"%s\",\"gain\":%u,\"identity\":\"%s\",\"header_crc32\":%lu,\"applied\":false,\"oem_radiometry_ready\":false,\"application_blocker\":\"runtime_adapter_unverified\"}",
                               (unsigned long)manifest.schema, (unsigned long)manifest.generation,
                               (unsigned long)manifest.payload_len, (unsigned long)manifest.payload_crc32,
                               (unsigned long)manifest.calibration_id, manifest.model, manifest.profile,
                               (unsigned)manifest.gain, identity, (unsigned long)manifest.header_crc32);
        if (n <= 0 || (size_t)n >= sizeof(client->request)) return send_json_status(client, 500, "Internal Server Error", "{\"error\":\"encode\"}");
        return send_json_status(client, 200, "OK", (const char *)client->request);
    }
    if (is_data) {
        t384_cal_manifest_t manifest;
        if (t384_cal_storage_manifest(&manifest) != T384_CAL_OK
            || manifest.payload_len > T384_CAL_STORAGE_MAX_PAYLOAD) {
            return send_json_status(client, 404, "Not Found", "{\"error\":\"unavailable\"}");
        }
        if (t384_cal_storage_read_data(0u, client->request, manifest.payload_len)
            != T384_CAL_OK) return send_json_status(client, 500, "Internal Server Error", "{\"error\":\"read\"}");
        return send_binary_status(client, 200, "OK", client->request, manifest.payload_len);
    }
    const bool is_put=strncmp(request,"PUT /api/v1/calibration/v1/data ",
                              sizeof("PUT /api/v1/calibration/v1/data ")-1u)==0;
    const bool is_commit=strncmp(request,"POST /api/v1/calibration/v1/commit ",
                                 sizeof("POST /api/v1/calibration/v1/commit ")-1u)==0;
    if ((is_put || is_commit) && (raw16_client!=NULL || t384_module_files_busy()))
        return send_json_status(client,409,"Conflict","{\"error\":\"stop_stream_and_module_read\"}");
    if (strncmp(request, "PUT /api/v1/calibration/v1/data ", sizeof("PUT /api/v1/calibration/v1/data ") - 1u) == 0) {
        if (!has_content_length) return send_json_status(client, 411, "Length Required", "{\"error\":\"content_length_required\"}");
        if (body_len > 3072u) return send_json_status(client, 413, "Payload Too Large", "{\"error\":\"body_too_large\"}");
        const t384_cal_status_t status=storage_put_packet((const uint8_t *)body,body_len);
        char result[96];
        snprintf(result,sizeof(result),"{\"staged\":%s,\"status\":\"%s\"}",
                 status==T384_CAL_OK?"true":"false",t384_cal_status_name(status));
        return send_json_status(client,status==T384_CAL_OK?200:status==T384_CAL_BUSY?409:422,
                                status==T384_CAL_OK?"OK":"Error",result);
    } else if (strncmp(request, "POST /api/v1/calibration/v1/commit ", sizeof("POST /api/v1/calibration/v1/commit ")-1u) == 0) {
        if (body_len != 0u) return send_json_status(client, 400, "Bad Request", "{\"error\":\"body_not_allowed\"}");
        const t384_cal_status_t status=t384_cal_storage_finish();
        char result[160];
        snprintf(result,sizeof(result),
                 "{\"committed\":%s,\"status\":\"%s\",\"applied\":false,\"oem_radiometry_ready\":false}",
                 status==T384_CAL_OK?"true":"false",t384_cal_status_name(status));
        return send_json_status(client,status==T384_CAL_OK?200:status==T384_CAL_FLASH?500:409,
                                status==T384_CAL_OK?"OK":"Error",result);
    } else if (strncmp(request, "POST /api/v1/calibration/v1/abort ", sizeof("POST /api/v1/calibration/v1/abort ")-1u) == 0) {
        if (body_len != 0u) return send_json_status(client, 400, "Bad Request", "{\"error\":\"body_not_allowed\"}");
        t384_cal_storage_abort();
        return send_json_status(client, 200, "OK", "{\"aborted\":true}");
    }
    return send_json_status(client, 404, "Not Found", "{\"error\":\"not_found\"}");
}

/* New API responses live in this client's request buffer until ACKed. This
 * avoids sharing mutable JSON storage between simultaneous status polls. */
static err_t module_reply(http_client_t *client, int code, const char *body)
{
    const int n = snprintf((char *)client->request, sizeof(client->request),
                           "HTTP/1.0 %d %s\r\nContent-Type: application/json\r\n"
                           "Cache-Control: no-store\r\nContent-Length: %lu\r\n"
                           "Connection: close\r\n\r\n%s", code,
                           code < 300 ? "OK" : "Error", (unsigned long)strlen(body), body);
    if (n <= 0 || (size_t)n >= sizeof(client->request)) {
        abort_client(client); return ERR_ABRT;
    }
    return send_static_response(client, (const char *)client->request, (size_t)n);
}

static err_t handle_module_request(http_client_t *client)
{
    t384_mf_request_t request;
    const int parsed = t384_module_files_parse_http((const char *)client->request,
                                                   client->request_length, &request);
    if (parsed == 0) return ERR_OK;
    if (parsed != 200) return module_reply(client, parsed, "{\"error\":\"invalid_request\"}");
    const t384_module_file_status_t *s = t384_module_files_status();
    if (request.action == T384_MF_HTTP_READ) {
        const int rc = t384_module_files_start(request.id, raw16_client != NULL, t384_millis());
        if (rc != 0) {
            char error_body[80];
            snprintf(error_body, sizeof(error_body), "{\"error\":\"start_rejected\",\"code\":%d}", rc);
            return module_reply(client, rc == -1 ? 400 : rc == -2 ? 409 : 503, error_body);
        }
    } else if (request.action == T384_MF_HTTP_ABORT) {
        if (request.transaction != s->transaction)
            return module_reply(client, 409, "{\"error\":\"stale_transaction\"}");
        for (unsigned i = 0; i < HTTP_CLIENTS; ++i)
            if (clients[i].module_download) abort_client(&clients[i]);
        t384_module_files_abort(t384_millis());
    } else if (request.action == T384_MF_HTTP_DATA) {
        uint8_t *buffer = t384_module_files_download(request.transaction);
        if (!buffer) return module_reply(client, 409, "{\"error\":\"not_ready_or_stale\"}");
        const int n = snprintf(client->header, sizeof(client->header),
                               "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\n"
                               "Content-Length: %lu\r\nCache-Control: no-store\r\n"
                               "X-T384-Transaction: %lu\r\nX-T384-CRC32: %08lx\r\n"
                               "Connection: close\r\n\r\n", (unsigned long)s->length,
                               (unsigned long)s->transaction, (unsigned long)s->crc32);
        if (n <= 0 || (size_t)n > T384_MODULE_FILE_HEADER_RESERVE) {
            t384_module_files_download_release(false);
            return module_reply(client, 500, "{\"error\":\"header_capacity\"}");
        }
        uint8_t *begin = buffer + T384_MODULE_FILE_HEADER_RESERVE - (size_t)n;
        memcpy(begin, client->header, (size_t)n);
        client->module_download = true;
        client->module_download_started = t384_millis();
        return send_static_response(client, (const char *)begin, (size_t)n + s->length);
    }
    char body[768], fw[23], tx_hex[47], rx_hex[65];
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < sizeof(s->fw); ++i)
        snprintf(fw+i*2u, 3u, "%02x", s->fw[i]);
    for (unsigned i = 0u; i < s->tx_diag_len; ++i) {
        tx_hex[i*2u] = hex[s->tx_diag[i] >> 4];
        tx_hex[i*2u+1u] = hex[s->tx_diag[i] & 0x0fu];
    }
    tx_hex[s->tx_diag_len*2u] = 0;
    for (unsigned i = 0u; i < s->rx_diag_len; ++i) {
        rx_hex[i*2u] = hex[s->rx_diag[i] >> 4];
        rx_hex[i*2u+1u] = hex[s->rx_diag[i] & 0x0fu];
    }
    rx_hex[s->rx_diag_len*2u] = 0;
    const char *const states[] = {"idle", "reading", "ready", "done", "error", "aborted"};
    const int n = snprintf(body, sizeof(body),
        "{\"state\":\"%s\",\"transaction\":%lu,\"id\":\"%s\",\"path\":\"%s\","
        "\"length\":%lu,\"received\":%lu,\"crc32\":%lu,\"crc32_source\":\"local\","
        "\"error\":%d,\"uart_status\":%u,\"open_status\":%u,\"close_status\":%u,"
        "\"command\":%u,\"error_command\":%u,\"error_uart_status\":%u,"
        "\"dvp_paused\":%s,\"downloading\":%s,\"cleanup_failed\":%s,"
        "\"pn\":\"%s\",\"sn\":\"%s\",\"fw_hex\":\"%s\",\"identity_verified\":%s,"
        "\"tx_hex\":\"%s\",\"rx_hex\":\"%s\"}",
        states[s->state], (unsigned long)s->transaction, s->id, s->path,
        (unsigned long)s->length, (unsigned long)s->received, (unsigned long)s->crc32,
        s->error, s->uart_status, s->open_status, s->close_status, s->command,
        s->error_command, s->error_uart_status,
        s->held ? "true" : "false", s->downloading ? "true" : "false",
        s->cleanup_failed ? "true" : "false", s->pn, s->sn, fw,
        s->state == T384_MF_READY || s->state == T384_MF_DONE ? "true" : "false",
        tx_hex, rx_hex);
    if (n <= 0 || (size_t)n >= sizeof(body))
        return module_reply(client, 500, "{\"error\":\"status_capacity\"}");
    return module_reply(client, request.action == T384_MF_HTTP_READ ? 202 : 200, body);
}

static err_t http_receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p,
    err_t error)
{
    http_client_t *client = (http_client_t *)arg;
    if (p == NULL) {
        /* A streaming peer may close while lwIP still owns a no-copy payload
         * pbuf.  Abort first so those pbufs are freed before the pipeline
         * lease is returned; ordinary HTTP responses keep graceful close. */
        if (client->raw16_response) {
            abort_client(client);
            return ERR_ABRT;
        }
        return close_client(client);
    }
    if (error != ERR_OK) {
        pbuf_free(p);
        abort_client(client);
        return ERR_ABRT;
    }

    client->idle_polls = 0u;
    if (client->raw16_response || client->static_response || client->closing) {
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    if (client->request_length + p->tot_len > sizeof(client->request) - 1u) {
        tcp_recved(pcb, p->tot_len); pbuf_free(p);
        return send_json_status(client, 413, "Payload Too Large", "{\"error\":\"request_too_large\"}");
    }
    const u16_t copied = pbuf_copy_partial(p, client->request + client->request_length,
                                           (u16_t)(sizeof(client->request) - 1u - client->request_length), 0u);
    client->request_length += copied;
    client->request[client->request_length] = '\0';
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    /* Wait for complete headers, including a request line split across TCP
     * packets. No module mutation can occur before framing is validated. */
    if (strstr((char *)client->request, "\r\n\r\n") == NULL) return ERR_OK;
    const char *first_space = strchr((char *)client->request, ' ');
    if (first_space && strncmp(first_space+1, "/api/v1/module-files/", 21u) == 0)
        return handle_module_request(client);

    if (strstr((char *)client->request, "GET /api/v1/device ") == (char *)client->request ||
        strstr((char *)client->request, "GET /api/v1/calibration/v1/") == (char *)client->request ||
        strstr((char *)client->request, "PUT /api/v1/calibration/v1/") == (char *)client->request ||
        strstr((char *)client->request, "POST /api/v1/calibration/v1/") == (char *)client->request) {
        if (strstr((char *)client->request, "\r\n\r\n") == NULL) return ERR_OK;
        return handle_calibration_request(client);
    }

    char request[64] = {0};
    const u16_t request_copy = client->request_length > 63u ? 63u : (u16_t)client->request_length;
    memcpy(request, client->request, request_copy);
    if (request_matches_path(request, request_copy, "/api/v1/network")) {
        const int n = snprintf((char *)client->request, sizeof(client->request),
            "{\"mode\":\"fixed_private_subnet\",\"segment\":%u,\"device_ip\":\"192.168.%u.1\","
            "\"prefix_length\":24,\"dhcp_first\":\"192.168.%u.2\",\"dhcp_last\":\"192.168.%u.20\","
            "\"default_segment\":17,\"configurable\":false}",
            T384_NCM_IPV4_C, T384_NCM_IPV4_C, T384_NCM_IPV4_C, T384_NCM_IPV4_C);
        if (n < 0 || (size_t)n >= sizeof(client->request))
            return send_json_status(client, 500, "Internal Server Error", "{}");
        return send_json_status(client, 200, "OK", (const char *)client->request);
    } else if (strncmp(request, "PUT /api/v1/network ", sizeof("PUT /api/v1/network ") - 1u) == 0) {
        return send_json_status(client, 501, "Not Implemented", "{\"ok\":false,\"error\":{\"code\":\"network_persistence_unavailable\",\"message\":\"Network storage and recovery are not implemented\"}}");
    } else if (strncmp(request, "GET /api/", sizeof("GET /api/") - 1u) == 0) {
        return send_json_status(client, 404, "Not Found", "{\"ok\":false,\"error\":{\"code\":\"not_found\",\"message\":\"API not implemented\"}}");
    } else if (request_matches_path(request, request_copy, "/hotspot-detect.html") ||
               request_matches_path(request, request_copy, "/library/test/success.html") ||
               request_matches_path(request, request_copy, "/success.html") ||
               request_matches_path(request, request_copy, "/generate_204") ||
               request_matches_path(request, request_copy, "/generate204") ||
               request_matches_path(request, request_copy, "/gen_204") ||
               request_matches_path(request, request_copy, "/connecttest.txt") ||
               request_matches_path(request, request_copy, "/ncsi.txt")) {
        const int n = snprintf((char *)client->request, sizeof(client->request),
            "HTTP/1.1 302 Found\r\nLocation: http://192.168.%u.1/\r\n"
            "Cache-Control: no-store\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", T384_NCM_IPV4_C);
        if (n < 0 || (size_t)n >= sizeof(client->request)) return ERR_VAL;
        return send_and_close(client, (const char *)client->request, (size_t)n);
    } else if (request_matches_path(request, request_copy, "/diag")) {
        const size_t length = build_diag_response();
        if (length != 0u) {
            return send_and_close(client, diag_response, length);
        }
    } else if (request_matches_path(request, request_copy, "/raw16.stream")) {
        return send_raw16_stream(client);
    } else if (request_matches_path(request, request_copy, "/")) {
        return send_static_response(client, status_response,
#if T384_NETWORK_ON_V5F
                                    sizeof(status_response));
#else
                                    sizeof(status_response) - 1u);
#endif
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
    (void)t384_cal_storage_init();
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
    for (unsigned i = 0; i < HTTP_CLIENTS; ++i) {
        if (clients[i].module_download &&
            (uint32_t)(now-clients[i].module_download_started) >= 15000u)
            abort_client(&clients[i]);
    }
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
    if (client == NULL) {
        for (unsigned drained = 0u;
             drained < HTTP_IDLE_PIPELINE_DRAIN_BUDGET; ++drained) {
            t384_frame_chunk_view_t chunk;
            if (!t384_frame_pipeline_peek(&chunk)) {
                break;
            }
            t384_frame_pipeline_release();
        }
        return;
    }

    if (client == NULL || client->pcb == NULL || client->closing) {
        abort_client(client);
        return;
    }
    /* Any RAW16 connection that makes no write progress is stale, even if
     * lwIP has not yet marked its PCB dead. This also covers a peer/browser
     * that stopped reading while the PCB remains nominally alive. */
    if ((uint32_t)(now - client->raw16_last_progress_ms) >=
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
