/* Real V3F proxy + V5F mailbox handler; deterministic scheduling instead of
 * hardware. Exercise both sides, cancellation races and file lease cleanup. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "t384_dualcore.h"

static uint32_t clock_ms;
static bool remote_running, schedule_remote, inject_late_cancel;
static uint32_t run_remote_at;
static unsigned starts, aborts, releases;
static t384_module_file_status_t file;

static void tick_remote(void)
{
    remote_running = true;
    t384_dualcore_capture_task();
    remote_running = false;
}

uint32_t t384_millis(void)
{
    if (!remote_running) {
        ++clock_ms;
        if (schedule_remote && clock_ms >= run_remote_at) tick_remote();
    }
    return clock_ms;
}

void t384_capture_source_task(void) { }
void t384_capture_source_get_stats(t384_frame_source_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    out->initialized = out->stream_ready = 1u;
    out->frame_mode = T384_FRAME_MODE_TPD_Y16;
    if (inject_late_cancel) {
        inject_late_cancel = false;
        __atomic_store_n(&t384_dualcore_shared.rpc_cancel, 1u, __ATOMIC_RELEASE);
    }
}

const t384_module_file_status_t *t384_capture_files_status(void) { return &file; }

int t384_capture_files_start(const char *id, bool stream_active, uint32_t now)
{
    assert(now == clock_ms);
    ++starts;
    if (stream_active || file.held) return -2;
    assert(id && strcmp(id, "nuct-high") == 0);
    file.held = true;
    file.state = T384_MF_READING;
    ++file.transaction;
    return 0;
}

void t384_capture_files_abort(uint32_t now)
{
    assert(now == clock_ms);
    ++aborts;
    file.held = false;
    file.state = T384_MF_ABORTED;
}

uint8_t *t384_capture_files_download(uint32_t transaction)
{
    if (file.state != T384_MF_READY || file.downloading || transaction != file.transaction)
        return NULL;
    file.downloading = true;
    return t384_dualcore_frame;
}

void t384_capture_files_download_release(bool complete)
{
    if (!file.downloading) return;
    ++releases;
    file.held = file.downloading = false;
    file.state = complete ? T384_MF_DONE : T384_MF_ABORTED;
}

static void submit(uint32_t command)
{
    t384_dualcore_shared_t *s = &t384_dualcore_shared;
    s->rpc_command = command;
    s->rpc_argument = file.transaction;
    s->rpc_stream_active = 0u;
    strcpy(s->rpc_id, "nuct-high");
    s->rpc_cancel = 0u;
    ++s->rpc_request;
}

int main(void)
{
    assert((uintptr_t)t384_dualcore_frame <= UINT32_MAX); /* -no-pie host fixture */
    memset(&t384_dualcore_shared, 0, sizeof(t384_dualcore_shared));
    t384_dualcore_shared.v5f_initialized = t384_dualcore_shared.v5f_booted = 1u;
    for (unsigned region = 0u; region < T384_FRAME_PROBE_REGIONS; ++region) {
        const uint32_t probe = T384_DTCM_PROBE + region;
        memcpy(t384_frame_probe_region(region), &probe, sizeof(probe));
    }
    t384_frame_source_task();
    assert(t384_dualcore_shared.v3f_frame_access_ok == 1u);
    schedule_remote = true;
    assert(t384_module_files_start("nuct-high", false, 0u) == 0);
    assert(t384_module_files_busy() && !t384_frame_source_stream_ready());
    assert(t384_module_files_start("nuct-high", true, 0u) == -2);
    t384_module_files_abort(0u);
    assert(!t384_module_files_busy() && t384_frame_source_stream_ready());

    assert(t384_module_files_start("nuct-high", false, 0u) == 0);
    file.state = T384_MF_READY;
    tick_remote();
    uint8_t *pointer = t384_module_files_download(file.transaction);
    assert(pointer == t384_dualcore_frame && file.downloading);
    pointer[255] = 0x37u;
    assert(t384_dualcore_frame[255] == 0x37u);
    t384_module_files_download_release(true);
    assert(!t384_module_files_busy() && file.state == T384_MF_DONE);

    /* Timeout before V5F handles START must never perform a late start. */
    schedule_remote = false;
    const unsigned before = starts;
    assert(t384_module_files_start("nuct-high", false, 0u) == -3);
    assert(starts == before);
    tick_remote();
    assert(starts == before && !file.held);

    /* Cancellation after execution but before ACK reclaims only this lease. */
    submit(T384_RPC_FILE_START);
    inject_late_cancel = true;
    tick_remote();
    assert(!file.held && aborts == 2u);

    /* An unsuccessful START must not abort somebody else's held table. */
    file.held = true;
    submit(T384_RPC_FILE_START);
    inject_late_cancel = true;
    tick_remote();
    assert(file.held && aborts == 2u);

    file.state = T384_MF_READY;
    submit(T384_RPC_FILE_DOWNLOAD);
    inject_late_cancel = true;
    tick_remote();
    assert(!file.held && !file.downloading && releases == 2u);

    /* RELEASE is cleanup: execute it even if the V3F wait timed out. */
    file.held = file.downloading = true;
    submit(T384_RPC_FILE_RELEASE);
    t384_dualcore_shared.rpc_cancel = 1u;
    tick_remote();
    assert(!file.held && !file.downloading && releases == 3u);
    puts("Dual-core actual proxy/handler: DTCM probe, file RPC, timeout/late cancel and lease cleanup passed");
    return 0;
}
