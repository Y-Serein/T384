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
static unsigned parameter_queries;
static unsigned state_queries;
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
        if (command[9] == 6) {
            const char *pn = scenario >= 16 ? "WN2384" : scenario == 10 ? "WN2256" : "WN2256[F1]";
            memcpy(reply+5, pn, strlen(pn));
        }
        else if (command[9] == 7) memcpy(reply+5, "test-sn-001", 11);
        else { assert(command[9] == 2); memcpy(reply+5, "00.00.08.03", 11); }
        if ((scenario == 6 || scenario == 22 || scenario==31) && info_queries == 4) reply[5] ^= 1;
    } else if (scenario>=25 && (command[6]==0x2F || command[6]==2 || command[6]==0x0F)) {
        ++state_queries;
        if (command[6]==0x2F) {
            assert(command[5]==1 && index==0x81 && n==1);
            reply[5]=(scenario==30 && state_queries==5)?0:1;
        } else if (command[6]==0x0F) {
            assert(command[5]==1 && index==0x86 && n==2);
            reply[5]=0x68; reply[6]=0x1D;
        } else { assert(command[5]==0x10 && n==1 && (index==0x81 || index==0x83)); reply[5]=1; }
        if (state_queries==1) {
            if (scenario==26) { reply[4]=1; n=0; }
            if (scenario==28) n=2;
            if (scenario==29) { reply_size=reply_offset=0; return; }
        }
    } else if (command[6] == 0x26) {
        assert(command[5] == 1 && index == 0x8A && n == 6);
        assert(command[9] == (scenario == 17 ? 0 : 1));
        assert(memcmp(command+10, "\0\0\0\0\0\0\0", 7) == 0);
        memcpy(reply+5, "\xfe\xff\xfd\xff\x34\x12", 6);
        ++parameter_queries;
        if (scenario == 18) { reply[4] = 1; n = 0; }
        if (scenario == 20) n = 4;
        if (scenario == 21) { reply_size = reply_offset = 0; return; }
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
    if ((scenario == 1 && index == 0x86) || (scenario == 19 && index == 0x8A)) reply[5] ^= 0x80;
    if (scenario==27 && state_queries==1) reply[5]^=0x80;
    reply_size = n+9u; reply_offset = 0;
}
bool t384_module_file_port_tx(uint8_t value)
{
    assert(active && sent < sizeof(command));
    if (scenario == 23 && sent == 10 && command[7] == 0x8A &&
        t384_module_files_status()->error == 0) return false;
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

static void state_tests(void)
{
    assert(t384_module_files_start("cal-state",true,now)==-2);
    assert(t384_module_files_start("cal-state",false,now)==0);
    const t384_module_file_status_t *s=t384_module_files_status();
    for (;now<40000 && s->state==T384_MF_READING;++now) t384_module_files_task(now);
    assert(reads==0 && closes==0 && s->path[0]==0);
    assert(s->open_status==255 && s->close_status==255);
    if (scenario==25) {
        assert(state_queries==5 && info_queries==6 && s->state==T384_MF_READY);
        assert(s->length==8 && s->received==8);
        uint8_t *data=t384_module_files_download(s->transaction);
        assert(data && memcmp(data+T384_MODULE_FILE_HEADER_RESERVE,"\1\0\x68\x1D\1\0\1\0",8)==0);
        t384_module_files_download_release(true);
    } else {
        assert(s->state==T384_MF_ERROR && !s->held);
        assert(!t384_module_files_download(s->transaction));
        if (scenario==26 || scenario==30 || scenario==31) {
            assert(!s->cleanup_failed);
            scenario=25; state_queries=info_queries=0;
            assert(t384_module_files_start("cal-state",false,now)==0);
            for (;now<80000 && s->state==T384_MF_READING;++now) t384_module_files_task(now);
            assert(s->state==T384_MF_READY);
            t384_module_files_abort(now);
        } else {
            assert(s->cleanup_failed);
            assert(s->error==(scenario==27?-5:scenario==28?-7:-4));
            assert(t384_module_files_start("cal-state",false,now)==-2);
        }
    }
    assert(pauses==resumes && !active);
    puts("calibration state snapshot/identity/gain-change/refusal/CRC/length/timeout passed");
}

static void parameter_tests(void)
{
    const char *id = scenario == 17 ? "tpd-low" : "tpd-high";
    assert(t384_module_files_start(id, true, now) == -2);
    assert(pauses == 0);
    assert(t384_module_files_start(id, false, now) == 0);
    const t384_module_file_status_t *s = t384_module_files_status();
    for (now = 0; now < 40000 && s->state == T384_MF_READING; ++now) {
        t384_module_files_task(now);
        if (scenario == 23 && sent > 0 && command[7] == 0x8A)
            t384_module_files_abort(now);
    }
    assert(parameter_queries == 1 && reads == 0 && closes == 0);
    assert(s->path[0] == 0 && s->open_status == 255 && s->close_status == 255);
    if (scenario == 16 || scenario == 17 || scenario == 24) {
        assert(s->state == T384_MF_READY && info_queries == 6);
        assert(strcmp(s->pn, "WN2384") == 0 && s->length == 6 && s->received == 6);
        if (scenario == 24) {
            t384_module_files_abort(now);
            assert(s->state == T384_MF_ABORTED && !s->held);
        } else {
            uint8_t *data = t384_module_files_download(s->transaction);
            assert(data && memcmp(data+T384_MODULE_FILE_HEADER_RESERVE,
                                  "\xfe\xff\xfd\xff\x34\x12", 6) == 0);
            t384_module_files_download_release(true);
            assert(s->state == T384_MF_DONE && !s->held);
        }
        assert(!s->cleanup_failed);
    } else {
        assert((s->state == T384_MF_ERROR || s->state == T384_MF_ABORTED) && !s->held);
        assert(t384_module_files_download(s->transaction) == NULL);
        if (scenario == 18 || scenario == 22) {
            assert(!s->cleanup_failed);
            assert(s->error == (scenario == 18 ? -6 : -8));
            scenario = 16;
            assert(t384_module_files_start("tpd-high", false, now) == 0);
            for (; now < 80000 && s->state == T384_MF_READING; ++now)
                t384_module_files_task(now);
            assert(s->state == T384_MF_READY);
            t384_module_files_abort(now);
        } else {
            assert(s->cleanup_failed);
            assert(s->error_command == 0x8A);
            assert(s->error == (scenario == 19 ? -5 : scenario == 20 ? -7 : scenario == 21 ? -4 : -11));
            assert(t384_module_files_start("tpd-low", false, now) == -2);
        }
    }
    assert(pauses == resumes && !active && closes == 0 && reads == 0);
    puts("TPD parameters query/identity/refusal/CRC/length/timeout/abort protection passed");
}

int main(int argc, char **argv)
{
    scenario = argc > 1 ? (unsigned)atoi(argv[1]) : 0;
    const unsigned requested_scenario = scenario;
    parser_tests();
    t384_frame_pipeline_init();
    if (scenario>=25) { state_tests(); return 0; }
    if (scenario >= 16) { parameter_tests(); return 0; }
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
