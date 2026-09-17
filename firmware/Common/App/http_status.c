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

#define HTTP_CLIENTS 3u
#define HTTP_POLL_INTERVAL 4u
#define HTTP_IDLE_POLL_LIMIT 5u
#define HTTP_RAW16_WRITE_BUDGET 16u
#define HTTP_STATIC_WRITE_BUDGET 16u
#define HTTP_RAW16_STALL_TIMEOUT_MS 10000u
#define HTTP_RAW16_TARGET_BPS 4915200u
#define HTTP_IDLE_PIPELINE_DRAIN_BUDGET 32u

typedef struct {
    struct tcp_pcb *pcb;
    uint8_t idle_polls;
    bool closing;
    bool static_response;
    bool raw16_response;
    bool raw16_blocked;
    bool module_download;
    uint32_t module_download_started;
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
    uint8_t request[4096];
    size_t request_length;
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
static char diag_response[6080 + (T384_DUALCORE ? 256 : 0)];
/* The storage API caps payloads at 2 KiB; leave 256 B for HTTP headers. */
static char calibration_response[T384_CAL_STORAGE_MAX_PAYLOAD + 256u];
static uint8_t calibration_binary[T384_CAL_STORAGE_MAX_PAYLOAD];

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
    enum { DIAG_HEADER_RESERVE = 128 };
    int body_length = snprintf(
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
        "mini2.sn=%s\n"
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
        "roi.le_maximum_raw=%lu\n"
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
        "http.close_callback_isolation=1\n"
        "http.static_window_refill=1\n"
        "http.checksum_aligned_reads=1\n"
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
        "stream.write_errors=%lu\n"
        "stream.timeout_disconnects=%lu\n"
        "stream.fps_x1000=%lu\n"
        "stream.payload_bps=%lu\n",
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
        T384_MINI2_DIGITAL_RESPONSE_BYTES,
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
        source.mini2_sn,
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
        (unsigned long)roi_snapshot.le_maximum,
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
        (unsigned long)raw16_stats.write_errors,
        (unsigned long)raw16_stats.timeout_disconnects,
        (unsigned long)raw16_stats.fps_x1000,
        (unsigned long)raw16_stats.payload_bps);

    if (body_length < 0 ||
        (size_t)body_length >= sizeof(diag_response) - DIAG_HEADER_RESERVE) {
        return 0u;
    }

#if T384_DUALCORE
    const t384_dualcore_shared_t *shared = &t384_dualcore_shared;
    const int extra = snprintf(
        diag_response + DIAG_HEADER_RESERVE + body_length,
        sizeof(diag_response) - DIAG_HEADER_RESERVE - (size_t)body_length,
        "dualcore.v5f_booted=%lu\n"
        "dualcore.v5f_initialized=%lu\n"
        "dualcore.dtcm_access=%lu\n"
        "dualcore.frame_banks=2\n"
        "dualcore.frame_state=%lu\n"
        "dualcore.frame_state1=%lu\n",
        (unsigned long)__atomic_load_n(&shared->v5f_booted, __ATOMIC_ACQUIRE),
        (unsigned long)__atomic_load_n(&shared->v5f_initialized, __ATOMIC_ACQUIRE),
        (unsigned long)__atomic_load_n(&shared->v3f_frame_access_ok, __ATOMIC_ACQUIRE),
        (unsigned long)__atomic_load_n(&shared->banks[0].state, __ATOMIC_ACQUIRE),
        (unsigned long)__atomic_load_n(&shared->banks[1].state, __ATOMIC_ACQUIRE));
    if (extra < 0 || (size_t)extra >= sizeof(diag_response) -
        DIAG_HEADER_RESERVE - (size_t)body_length) return 0u;
    body_length += extra;
#endif
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
    if (client->module_download) {
        const bool complete = client->static_offset == client->static_length &&
                              client->static_inflight == 0u;
        t384_module_files_download_release(complete);
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
        } else {
            client->static_inflight = 0u;
        }
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
    const int header_length = snprintf(
        client->header, sizeof(client->header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/x-t384-frame-chunks\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "X-T384-Format: T384-FRAME-CHUNK-V1\r\n"
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
        frame_mode, t384_frame_pixel_format_name(pixel_format), pixel_format,
        y16 ? T384_EXPERIMENTAL_TEMP_MODEL : "unavailable",
        (unsigned long)T384_EXPERIMENTAL_Y16_ZERO_C_X100,
        (unsigned long)T384_EXPERIMENTAL_Y16_COUNTS_PER_C_X100,
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

static int storage_put_packet(const uint8_t *data, size_t len)
{
    if (!data || len < sizeof(t384_cal_manifest_t)) return -1;
    t384_cal_manifest_t manifest;
    memcpy(&manifest, data, sizeof(manifest));
    if (t384_cal_storage_begin(&manifest) != T384_CAL_OK) return -1;
    const uint32_t payload_len = manifest.payload_len;
    if ((size_t)payload_len != len - sizeof(manifest)
        || t384_cal_storage_write(0u, data + sizeof(manifest), payload_len)
               != T384_CAL_OK) {
        t384_cal_storage_abort();
        return -1;
    }
    return 0;
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
    const char *cl = strstr(request, "Content-Length:");
    if (cl != NULL && cl < end) {
        cl += 15;
        while (*cl == ' ' || *cl == '\t') ++cl;
        while (*cl >= '0' && *cl <= '9') {
            if (content_length > 4096u) return send_json_status(client, 413, "Payload Too Large", "{\"error\":\"body_too_large\"}");
            content_length = content_length * 10u + (size_t)(*cl - '0');
            ++cl;
        }
    }
    if (content_length > 3072u) return send_json_status(client, 413, "Payload Too Large", "{\"error\":\"body_too_large\"}");
    if (body_len < content_length) return ERR_OK;
    body_len = content_length;
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
        const int n = snprintf((char *)client->request, sizeof(client->request),
                               "{\"schema\":%lu,\"generation\":%lu,\"payload_len\":%lu,\"payload_crc32\":%lu,\"calibration_id\":%lu,\"model\":\"%s\",\"profile\":\"%s\",\"gain\":%u}",
                               (unsigned long)manifest.schema, (unsigned long)manifest.generation,
                               (unsigned long)manifest.payload_len, (unsigned long)manifest.payload_crc32,
                               (unsigned long)manifest.calibration_id, manifest.model, manifest.profile,
                               (unsigned)manifest.gain);
        if (n <= 0 || (size_t)n >= sizeof(client->request)) return send_json_status(client, 500, "Internal Server Error", "{\"error\":\"encode\"}");
        return send_json_status(client, 200, "OK", (const char *)client->request);
    }
    if (is_data) {
        t384_cal_manifest_t manifest;
        if (t384_cal_storage_manifest(&manifest) != T384_CAL_OK
            || manifest.payload_len > sizeof(calibration_binary)) {
            return send_json_status(client, 404, "Not Found", "{\"error\":\"unavailable\"}");
        }
        if (t384_cal_storage_read_data(0u, calibration_binary, manifest.payload_len)
            != T384_CAL_OK) return send_json_status(client, 500, "Internal Server Error", "{\"error\":\"read\"}");
        return send_binary_status(client, 200, "OK", calibration_binary, manifest.payload_len);
    }
    int rc = -1;
    if (strncmp(request, "PUT /api/v1/calibration/v1/data ", sizeof("PUT /api/v1/calibration/v1/data ") - 1u) == 0) {
        if (cl == NULL) return send_json_status(client, 411, "Length Required", "{\"error\":\"content_length_required\"}");
        if (body_len > 3072u) return send_json_status(client, 413, "Payload Too Large", "{\"error\":\"body_too_large\"}");
        rc = storage_put_packet((const uint8_t *)body, body_len);
        return send_json_status(client, rc == 0 ? 200 : 422, rc == 0 ? "OK" : "Unprocessable Entity", rc == 0 ? "{\"staged\":true}" : "{\"error\":\"rejected\"}");
    } else if (strncmp(request, "POST /api/v1/calibration/v1/commit ", 35u) == 0) {
        if (body_len != 0u) return send_json_status(client, 400, "Bad Request", "{\"error\":\"body_not_allowed\"}");
        rc = t384_cal_storage_finish() == T384_CAL_OK ? 0 : -1;
        return send_json_status(client, rc == 0 ? 200 : 409,
                                rc == 0 ? "OK" : "Conflict",
                                rc == 0 ? "{\"committed\":true}" : "{\"error\":\"commit_failed\"}");
    } else if (strncmp(request, "POST /api/v1/calibration/v1/abort ", 34u) == 0) {
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
    char body[768], fw[23];
    for (unsigned i = 0; i < sizeof(s->fw); ++i)
        snprintf(fw+i*2u, 3u, "%02x", s->fw[i]);
    const char *const states[] = {"idle", "reading", "ready", "done", "error", "aborted"};
    const int n = snprintf(body, sizeof(body),
        "{\"state\":\"%s\",\"transaction\":%lu,\"id\":\"%s\",\"path\":\"%s\","
        "\"length\":%lu,\"received\":%lu,\"crc32\":%lu,\"crc32_source\":\"local\","
        "\"error\":%d,\"uart_status\":%u,\"open_status\":%u,\"close_status\":%u,"
        "\"command\":%u,\"error_command\":%u,\"error_uart_status\":%u,"
        "\"dvp_paused\":%s,\"downloading\":%s,\"cleanup_failed\":%s,"
        "\"pn\":\"%s\",\"sn\":\"%s\",\"fw_hex\":\"%s\",\"identity_verified\":%s}",
        states[s->state], (unsigned long)s->transaction, s->id, s->path,
        (unsigned long)s->length, (unsigned long)s->received, (unsigned long)s->crc32,
        s->error, s->uart_status, s->open_status, s->close_status, s->command,
        s->error_command, s->error_uart_status,
        s->held ? "true" : "false", s->downloading ? "true" : "false",
        s->cleanup_failed ? "true" : "false", s->pn, s->sn, fw,
        s->state == T384_MF_READY || s->state == T384_MF_DONE ? "true" : "false");
    if (n <= 0 || (size_t)n >= sizeof(body))
        return module_reply(client, 500, "{\"error\":\"status_capacity\"}");
    return module_reply(client, request.action == T384_MF_HTTP_READ ? 202 : 200, body);
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
