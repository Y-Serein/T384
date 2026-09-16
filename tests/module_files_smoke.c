#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t384_module_files.h"
#include "t384_module_files_http.h"
#include "t384_mini2_protocol.h"
#include "t384_frame_pipeline.h"

static uint8_t command[279], reply[521];
static size_t sent, reply_size, reply_offset;
static unsigned pauses, resumes, reads, closes, info_queries;
static unsigned now, scenario;
static bool active;
static uint32_t file_length = 32768u;

bool t384_module_file_port_pause(uint8_t **data, size_t *capacity)
{
    assert(!active);
    if (!t384_frame_pipeline_scratch_acquire(data, capacity)) return false;
    active = true; ++pauses;
    return true;
}
void t384_module_file_port_resume(void)
{
    assert(active); active = false; ++resumes;
    t384_frame_pipeline_scratch_release();
}
static void make_reply(void)
{
    const uint8_t index = command[7];
    uint16_t n = (uint16_t)command[17] | ((uint16_t)command[18] << 8);
    assert(command[0] == 0x55 && command[1] == 0x43 && command[2] == 0x49);
    assert(t384_mini2_crc16_xmodem(command+5, 16) ==
           ((uint16_t)command[21] | ((uint16_t)command[22] << 8)));
    memset(reply, 0, sizeof(reply));
    if (command[6] == 1) {
        assert(index == 0x81);
        ++info_queries;
        if (command[9] == 6) memcpy(reply+5, scenario == 10 ? "WN2256" : "WN2256[F1]", scenario == 10 ? 6 : 10);
        else if (command[9] == 7) memcpy(reply+5, "test-sn-001", 11);
        else { assert(command[9] == 2); memcpy(reply+5, "00.00.08.03", 11); }
        if (scenario == 6 && info_queries == 4) reply[5] ^= 1;
    } else {
        assert(command[5] == 0x10 && command[6] == 8);
        if (index == 0xC7) {
            assert(sent == 279 && n == 256);
            assert(memcmp(command+9, "\0\0\0\0\0\0\0\0", 8) == 0);
            assert(t384_mini2_crc16_xmodem(command+23, 256) ==
                   ((uint16_t)command[19] | ((uint16_t)command[20] << 8)));
            assert(strstr((const char *)command+23, "..") == NULL);
            n = 0;
        } else if (index == 0x87) {
            assert(n == 5);
            reply[5] = scenario == 2 ? 0 : scenario == 13 ? 1 : 2;
            const uint32_t length = scenario == 3 ? 32770u : file_length;
            for (unsigned i = 0; i < 4; ++i) reply[6+i] = (uint8_t)(length >> (8*i));
        } else if (index == 0x86) {
            assert(n && n <= 512);
            uint32_t offset = 0;
            for (unsigned i = 0; i < 4; ++i) offset |= (uint32_t)command[13+i] << (8*i);
            assert(offset == reads*512u && offset+n <= file_length);
            for (unsigned i = 0; i < n; ++i) reply[5+i] = (uint8_t)(offset+i);
            ++reads;
        } else {
            assert(index == 0x46 && n == 0); ++closes;
            if (scenario == 7) reply[4] = 4;
        }
    }
    if (scenario == 4 && index == 0x86) { reply_size = reply_offset = 0; return; }
    if (scenario == 5 && index == 0x86) { reply[4] = 7; n = 0; }
    if (scenario == 14 && index == 0xC7) reply[4] = 1;
    if (scenario == 15 && index == 0xC7) { reply_size = reply_offset = 0; return; }
    reply[0] = 0xBE; reply[1] = 0xAA;
    reply[2] = (uint8_t)(n+1u); reply[3] = (uint8_t)((n+1u) >> 8);
    const uint16_t crc = t384_mini2_crc16_xmodem(reply, n+5u);
    reply[n+5u] = (uint8_t)crc; reply[n+6u] = (uint8_t)(crc >> 8);
    reply[n+7u] = 0xEB; reply[n+8u] = 0xAA;
    if (scenario == 1 && index == 0x86) reply[5] ^= 0x80;
    reply_size = n+9u; reply_offset = 0;
}
bool t384_module_file_port_tx(uint8_t value)
{
    assert(active && sent < sizeof(command));
    command[sent++] = value;
    if (sent >= 5u && sent == 5u+command[3]+((size_t)command[4] << 8)) {
        make_reply(); sent = 0;
    }
    return true;
}
int t384_module_file_port_rx(void)
{
    if (reply_offset == reply_size) return -1;
    return reply[reply_offset++];
}
static void parser_tests(void)
{
    t384_mf_request_t out;
    const char *valid = "POST /api/v1/module-files/read HTTP/1.1\r\nContent-Length: 9\r\nContent-Type: application/octet-stream\r\n\r\nnuct-high";
    char buffer[1024];
    for (size_t i = 0; i < strlen(valid); ++i) {
        memcpy(buffer, valid, i); buffer[i] = 0;
        assert(t384_module_files_parse_http(buffer, i, &out) == 0);
    }
    assert(t384_module_files_parse_http(valid, strlen(valid), &out) == 200);
    assert(out.action == T384_MF_HTTP_READ && strcmp(out.id, "nuct-high") == 0);
    const char *bad[] = {
        "POST /api/v1/module-files/read HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx",
        "POST /api/v1/module-files/read HTTP/1.1\r\nContent-Length: 42949672960\r\n\r\n",
        "POST /api/v1/module-files/read HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
        "GET /api/v1/module-files/status HTTP/1.1\r\nOrigin: https://example.org\r\n\r\n",
        "GET /api/v1/module-files/data?transaction=0 HTTP/1.1\r\n\r\n",
        "GET /api/v1/module-files/data?transaction=1&path=x HTTP/1.1\r\n\r\n",
        "POST /api/v1/module-files/abort HTTP/1.1\r\nContent-Length: 0\r\nContent-Type: application/octet-stream\r\n\r\n",
        "GET /api/v1/module-files/status HTTP/1.1\r\n\r\nx"
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i)
        assert(t384_module_files_parse_http(bad[i], strlen(bad[i]), &out) >= 400);
}

int main(int argc, char **argv)
{
    scenario = argc > 1 ? (unsigned)atoi(argv[1]) : 0;
    const unsigned requested_scenario = scenario;
    parser_tests();
    t384_frame_pipeline_init();
    assert(t384_module_files_start("../secret", false, now) == -1);
    assert(t384_module_files_start("nuct-high", true, now) == -2);
    assert(pauses == 0);
    if (scenario == 11) file_length = 2042;
    const char *id = scenario == 10 ? "distance-high" : "nuct-high";
    assert(t384_module_files_start(id, false, now) == 0);
    assert(t384_module_files_start(id, false, now) == -2);
    assert(!t384_frame_pipeline_begin_frame(0, 0, T384_CHUNK_FLAG_TPD_Y16));
    const t384_module_file_status_t *s = t384_module_files_status();
    for (now = 0; now < 40000 && s->state == T384_MF_READING; ++now) {
        t384_module_files_task(now);
        if (scenario == 8 && reads == 1) t384_module_files_abort(now);
        if (scenario == 12 && sent > 40 && command[7] == 0xC7) t384_module_files_abort(now);
    }
    if (scenario == 0 || scenario == 9 || scenario == 11) {
        assert(s->state == T384_MF_READY && s->received == file_length);
        assert(s->held && active && info_queries == 6 && closes == 1);
        if (scenario == 9) {
            t384_module_files_task(now+60000u);
            assert(s->state == T384_MF_ERROR && !s->held);
        } else {
            assert(t384_module_files_download(s->transaction+1u) == NULL);
            uint8_t *data = t384_module_files_download(s->transaction);
            assert(data && t384_module_files_download(s->transaction) == NULL);
            for (unsigned i = 0; i < file_length; ++i)
                assert(data[T384_MODULE_FILE_HEADER_RESERVE+i] == (uint8_t)i);
            /* CRC32 independently checked against Python/zlib golden value. */
            if (scenario == 0) assert(s->crc32 == 0x217726B2u);
            t384_module_files_task(now+60000u);
            assert(active); /* HTTP owns the buffer until its ACK/abort */
            t384_module_files_download_release(true);
            assert(s->state == T384_MF_DONE && !s->held);
        }
    } else {
        assert(s->state == T384_MF_ERROR || s->state == T384_MF_ABORTED);
        assert(t384_module_files_download(s->transaction) == NULL);
        assert(!s->held && !active);
        if (scenario == 14) {
            assert(closes == 0 && reads == 0 && s->error == -6);
            assert(!s->cleanup_failed && s->error_command == 0xC7 && s->error_uart_status == 1);
            assert(s->close_status == 255);
        } else if (scenario == 10) {
            assert(closes == 0 && reads == 0 && s->error == -9);
            assert(!s->cleanup_failed);
        }
        else assert(closes == 1);
        if (scenario == 5) assert(s->error == -6); /* close ACK must not erase error */
        if (scenario == 7) {
            assert(s->cleanup_failed);
            assert(t384_module_files_start(id, false, now) == -2);
        }
    }
    assert(pauses == 1 && resumes == 1);
    if (scenario == 14 || scenario == 10) {
        /* A rejected open or local path rejection must permit the next read. */
        scenario = 0;
        reads = info_queries = 0;
        assert(t384_module_files_start("nuct-low", false, now) == 0);
        const unsigned deadline = now + 10000;
        for (; now < deadline && s->state == T384_MF_READING; ++now)
            t384_module_files_task(now);
        assert(s->state == T384_MF_READY && s->length == 32768 && closes == 1);
        assert(t384_module_files_download(s->transaction));
        t384_module_files_download_release(true);
        assert(pauses == 2 && resumes == 2 && !s->cleanup_failed);
    }
    assert(t384_frame_pipeline_begin_frame(1, 1, T384_CHUNK_FLAG_TPD_Y16));
    printf("module file smoke scenario %u passed\n", requested_scenario);
    return 0;
}
