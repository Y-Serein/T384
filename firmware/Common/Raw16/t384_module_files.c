#include "t384_module_files.h"
#include "t384_mini2_protocol.h"

#include <stdio.h>
#include <string.h>

/* Protocol evidence: docs/reference/MINI2_READONLY_FILE_PROTOCOL.md.
 * No file data writes, close-all, CRC modification or calibration commands. */
enum { BEFORE_PN, BEFORE_SN, BEFORE_FW, OPEN, INFO, READ, CLOSE,
       AFTER_PN, AFTER_SN, AFTER_FW, DRAIN };
enum { BAD_ID = -1, BUSY = -2, UNAVAILABLE = -3, TIMEOUT = -4,
       BAD_FRAME = -5, MODULE_ERROR = -6, BAD_LENGTH = -7,
       IDENTITY = -8, NO_SUFFIX = -9, UART_ERROR = -10, CANCELLED = -11 };
static const char *const ids[] = {
    "kt-high", "kt-low", "bt-high", "bt-low", "nuct-high", "nuct-low",
    "distance-high", "distance-low"
};
static t384_module_file_status_t status;
static uint8_t *scratch;
static uint8_t tx[T384_MINI2_FILE_OPEN_BYTES];
static uint8_t rx[T384_MINI2_FILE_BLOCK_BYTES + 9u];
static uint8_t phase, selected, file_id;
static uint16_t tx_length, tx_offset, rx_length, expected_data;
static uint32_t started, command_started, ready_at, quiet_at, drain_started;
static bool pending, may_be_open;
static uint8_t identity_raw[3][32];
static uint16_t identity_length[3];

static uint32_t crc_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return crc;
}

static void release(void)
{
    if (status.held) t384_module_file_port_resume();
    status.held = false;
    status.downloading = false;
    scratch = NULL;
    pending = false;
}

static void failed_done(void)
{
    status.state = status.error == CANCELLED ? T384_MF_ABORTED : T384_MF_ERROR;
    release();
}

static void fail(int error, uint32_t now)
{
    if (status.error == 0) {
        status.error = error;
        status.error_command = status.command;
        status.error_uart_status = status.uart_status;
    }
    pending = false;
    if (phase == CLOSE || (phase == OPEN && may_be_open && tx_offset < tx_length)) {
        status.cleanup_failed = true;
        failed_done();
    } else if (may_be_open) {
        /* A timed-out read may still be arriving. Drain before a close so a
         * delayed read response cannot be mistaken for its acknowledgement. */
        phase = DRAIN;
        quiet_at = now;
        drain_started = now;
    } else {
        failed_done();
    }
}

static bool text_identity(char *out, const uint8_t *data, uint16_t length)
{
    size_t n = 0;
    while (n < length && data[n] != 0u) {
        if (data[n] < 0x20u || data[n] > 0x7Eu ||
            data[n] == '"' || data[n] == '\\') return false;
        out[n] = (char)data[n];
        ++n;
    }
    out[n] = 0;
    return n != 0;
}

static bool make_path(void)
{
    const char *gain = (selected & 1u) ? "low" : "high";
    if (selected < 6u) {
        static const char *const names[] = {"tpd_kt1.bin", "tpd_bt1.bin", "tpd_nuc_t.bin"};
        const int n = snprintf(status.path, sizeof(status.path),
                               "default_data/%s_gain/%s", gain, names[selected / 2u]);
        return n > 0 && (size_t)n < sizeof(status.path);
    }
    /* No guessed F1 fallback. Only an explicit, path-safe PN suffix. */
    const char *begin = strrchr(status.pn, '[');
    const char *end = strrchr(status.pn, ']');
    if (!begin || !end || end <= begin + 1 || end[1] != 0 || end-begin > 17)
        return false;
    char suffix[17] = {0};
    for (const char *p = begin + 1; p < end; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
        suffix[p-begin-1] = *p;
    }
    const int n = snprintf(status.path, sizeof(status.path),
                           "system_data/tpd_dis_table/%s%s.bin", gain, suffix);
    return n > 0 && (size_t)n < sizeof(status.path);
}

static void issue(uint32_t now)
{
    tx_length = 23u;
    tx_offset = rx_length = 0u;
    expected_data = 0u;
    if (phase <= BEFORE_FW || (phase >= AFTER_PN && phase <= AFTER_FW)) {
        const unsigned index = phase <= BEFORE_FW ? phase : phase - AFTER_PN;
        const uint8_t commands[] = {6u, 7u, 2u};
        expected_data = index == 2u ? 11u : 32u;
        t384_mini2_build_info_query_command(tx, commands[index], (uint8_t)expected_data);
    } else if (phase == OPEN) {
        if (!make_path()) { fail(NO_SUFFIX, now); return; }
        if (!t384_mini2_build_file_open(tx, file_id, status.path)) {
            fail(BAD_ID, now); return;
        }
        tx_length = sizeof(tx);
        may_be_open = true; /* even a lost/partial ACK requires cleanup */
    } else if (phase == INFO) {
        expected_data = 5u;
        t384_mini2_build_file_info(tx, file_id);
    } else if (phase == READ) {
        uint32_t left = status.length - status.received;
        expected_data = (uint16_t)(left > T384_MINI2_FILE_BLOCK_BYTES ?
                                  T384_MINI2_FILE_BLOCK_BYTES : left);
        if (!t384_mini2_build_file_read(tx, file_id, status.received, expected_data)) {
            fail(BAD_LENGTH, now); return;
        }
    } else if (phase == CLOSE) {
        t384_mini2_build_file_close(tx, file_id);
    }
    status.command = tx[7];
    status.uart_status = 0xFFu;
    command_started = now;
    pending = true;
}

static void response(uint32_t now)
{
    uint8_t result;
    uint16_t length;
    const uint8_t *data;
    if (!t384_mini2_parse_response(rx, rx_length, &result, &data, &length)) {
        fail(BAD_FRAME, now); return;
    }
    pending = false;
    status.uart_status = result;
    if (phase == CLOSE) status.close_status = result;
    /* A complete, CRC-valid, empty C7 rejection did not open this handle.
     * Do not turn the expected missing-file error into a failed CLOSE lock.
     * Lost, malformed and unexpected responses still require strict cleanup. */
    if (phase == OPEN && result != 0u && length == 0u) {
        may_be_open = false;
        fail(MODULE_ERROR, now);
        return;
    }
    if (result != 0u) { fail(MODULE_ERROR, now); return; }
    if (length != expected_data) { fail(BAD_LENGTH, now); return; }
    if (status.error != 0 && phase != CLOSE) { fail(status.error, now); return; }
    if (phase <= BEFORE_FW) {
        memcpy(identity_raw[phase], data, length);
        identity_length[phase] = length;
        if (phase == BEFORE_PN && !text_identity(status.pn, data, length)) {
            fail(IDENTITY, now); return;
        }
        if (phase == BEFORE_SN && !text_identity(status.sn, data, length)) {
            fail(IDENTITY, now); return;
        }
        if (phase == BEFORE_FW) memcpy(status.fw, data, 11u);
        ++phase;
    } else if (phase == OPEN) {
        phase = INFO;
    } else if (phase == INFO) {
        status.open_status = data[0];
        status.length = (uint32_t)data[1] | ((uint32_t)data[2] << 8) |
                        ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 24);
        /* SDK rejects a handle with flag 1 in adv_cfg_file_read (write only). */
        if (status.open_status == 0u || status.open_status == 1u) {
            fail(MODULE_ERROR, now); return;
        }
        if (status.length == 0u || status.length > T384_MODULE_FILE_MAX_BYTES ||
            (status.length & 1u) != 0u) { fail(BAD_LENGTH, now); return; }
        phase = READ;
    } else if (phase == READ) {
        memcpy(scratch + T384_MODULE_FILE_HEADER_RESERVE + status.received, data, length);
        status.crc32 = crc_update(status.crc32, data, length);
        status.received += length;
        if (status.received == status.length) phase = CLOSE;
    } else if (phase == CLOSE) {
        may_be_open = false;
        if (status.error != 0) { failed_done(); return; }
        phase = AFTER_PN;
    } else if (phase >= AFTER_PN && phase <= AFTER_FW) {
        const unsigned index = phase - AFTER_PN;
        if (length != identity_length[index] || memcmp(data, identity_raw[index], length)) {
            fail(IDENTITY, now); return;
        }
        if (phase == AFTER_FW) {
            status.crc32 ^= 0xFFFFFFFFu;
            status.state = T384_MF_READY;
            ready_at = now;
        } else ++phase;
    }
}

int t384_module_files_start(const char *id, bool stream_active, uint32_t now)
{
    if (stream_active || t384_module_files_busy() || status.cleanup_failed) return BUSY;
    unsigned index;
    for (index = 0; index < sizeof(ids)/sizeof(ids[0]); ++index)
        if (id && strcmp(id, ids[index]) == 0) break;
    if (index == sizeof(ids)/sizeof(ids[0])) return BAD_ID;
    size_t capacity = 0;
    if (!t384_module_file_port_pause(&scratch, &capacity)) return UNAVAILABLE;
    if (!scratch || capacity < T384_MODULE_FILE_MAX_BYTES + T384_MODULE_FILE_HEADER_RESERVE) {
        t384_module_file_port_resume(); scratch = NULL; return UNAVAILABLE;
    }
    uint32_t transaction = status.transaction + 1u;
    if (transaction == 0u) transaction = 1u;
    memset(&status, 0, sizeof(status));
    status.transaction = transaction;
    status.held = true;
    status.state = T384_MF_READING;
    status.crc32 = 0xFFFFFFFFu;
    status.uart_status = status.open_status = status.close_status = 0xFFu;
    strcpy(status.id, ids[index]);
    selected = (uint8_t)index;
    file_id = (uint8_t)(transaction % 99u + 1u);
    phase = BEFORE_PN;
    pending = may_be_open = false;
    started = now;
    return 0;
}

void t384_module_files_task(uint32_t now)
{
    if (status.state == T384_MF_READY) {
        if (!status.downloading && (uint32_t)(now-ready_at) >= 60000u) {
            status.error = TIMEOUT; failed_done();
        }
        return;
    }
    if (status.state != T384_MF_READING) return;
    if (phase == DRAIN) {
        for (unsigned i = 0; i < 64u; ++i) {
            if (t384_module_file_port_rx() == -1) break;
            quiet_at = now;
        }
        if ((uint32_t)(now-drain_started) >= 4000u) {
            status.cleanup_failed = true; failed_done();
        } else if ((uint32_t)(now-quiet_at) >= 2100u) phase = CLOSE;
        return;
    }
    if (status.error == 0 && (uint32_t)(now-started) >= 30000u) {
        fail(TIMEOUT, now); return;
    }
    if (!pending) { issue(now); return; }
    if ((uint32_t)(now-command_started) >= 2000u) { fail(TIMEOUT, now); return; }
    for (unsigned i = 0; i < 64u; ++i) {
        const int value = t384_module_file_port_rx();
        if (value == -1) break;
        if (value < 0) { fail(UART_ERROR, now); return; }
        if (tx_offset != tx_length || rx_length == sizeof(rx)) {
            fail(BAD_FRAME, now); return;
        }
        rx[rx_length++] = (uint8_t)value;
        if ((rx_length == 1u && rx[0] != 0xBEu) ||
            (rx_length == 2u && rx[1] != 0xAAu)) { fail(BAD_FRAME, now); return; }
        if (rx_length >= 4u) {
            const uint16_t payload = (uint16_t)rx[2] | ((uint16_t)rx[3] << 8);
            if (payload == 0u || payload > expected_data + 1u) {
                fail(BAD_LENGTH, now); return;
            }
            if (rx_length == payload + 8u) { response(now); return; }
        }
    }
    for (unsigned i = 0; i < 64u && tx_offset < tx_length; ++i) {
        if (!t384_module_file_port_tx(tx[tx_offset])) break;
        ++tx_offset;
    }
}

void t384_module_files_abort(uint32_t now)
{
    if (status.downloading) return; /* HTTP must release its pointer first */
    if (status.state == T384_MF_READY) {
        status.error = CANCELLED; failed_done();
    } else if (status.state == T384_MF_READING && status.error == 0) {
        /* Finish an in-flight OPEN packet before cleanup: otherwise CLOSE
         * bytes could be consumed as the remaining file-name payload. */
        if (pending && tx_offset < tx_length) {
            status.error = CANCELLED;
        } else fail(CANCELLED, now);
    }
}

const t384_module_file_status_t *t384_module_files_status(void) { return &status; }
bool t384_module_files_busy(void) { return status.held; }

uint8_t *t384_module_files_download(uint32_t transaction)
{
    if (status.state != T384_MF_READY || status.downloading ||
        status.transaction != transaction) return NULL;
    status.downloading = true;
    return scratch;
}

void t384_module_files_download_release(bool complete)
{
    if (!status.downloading) return;
    status.state = complete ? T384_MF_DONE : T384_MF_ABORTED;
    if (!complete) status.error = CANCELLED;
    release();
}
