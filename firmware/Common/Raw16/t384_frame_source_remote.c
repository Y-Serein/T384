#include "t384_dualcore.h"

#if T384_DUALCORE && defined(Core_V3F)
#include <string.h>
#include "t384_compiler.h"
#include "t384_time.h"

static t384_frame_source_stats_t source_snapshot;
static t384_module_file_status_t file_snapshot;
static uint32_t cached_snapshot_sequence = UINT32_MAX;

static void read_snapshot(void)
{
    t384_dualcore_shared_t *s = &t384_dualcore_shared;
    for (unsigned attempt = 0u; attempt < 16u; ++attempt) {
        const uint32_t before = __atomic_load_n(&s->snapshot_sequence, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        if (before == cached_snapshot_sequence) return;
        t384_frame_source_stats_t source = s->source;
        t384_module_file_status_t file = s->file;
        T384_MEMORY_BARRIER();
        if (before == __atomic_load_n(&s->snapshot_sequence, __ATOMIC_ACQUIRE)) {
            source_snapshot = source;
            file_snapshot = file;
            cached_snapshot_sequence = before;
            return;
        }
    }
}

static int request(uint32_t command, uint32_t argument, const char *id,
                   bool stream_active)
{
    t384_dualcore_shared_t *s = &t384_dualcore_shared;
    const uint32_t previous = __atomic_load_n(&s->rpc_request, __ATOMIC_ACQUIRE);
    if (!__atomic_load_n(&s->v5f_initialized, __ATOMIC_ACQUIRE) ||
        previous != __atomic_load_n(&s->rpc_ack, __ATOMIC_ACQUIRE)) return -3;
    if (id && strlen(id) >= sizeof(s->rpc_id)) return -1;
    memset(s->rpc_id, 0, sizeof(s->rpc_id));
    if (id) memcpy(s->rpc_id, id, strlen(id));
    s->rpc_command = command;
    s->rpc_argument = argument;
    s->rpc_stream_active = stream_active;
    __atomic_store_n(&s->rpc_cancel, 0u, __ATOMIC_RELEASE);
    const uint32_t sequence = previous + 1u;
    const uint32_t start = t384_millis();
    unsigned spins = 0u;
    __atomic_store_n(&s->rpc_request, sequence, __ATOMIC_RELEASE);
    while (__atomic_load_n(&s->rpc_ack, __ATOMIC_ACQUIRE) != sequence) {
        if ((uint32_t)(t384_millis() - start) >= 100u || ++spins >= 1000000u) {
            __atomic_store_n(&s->rpc_cancel, 1u, __ATOMIC_RELEASE);
            /* Never hand out a pointer after cancellation: V5F may already
             * have seen the flag and reclaimed the scratch lease. */
            return -3;
        }
    }
    read_snapshot();
    if (__atomic_load_n(&s->rpc_cancel, __ATOMIC_ACQUIRE) != 0u) return -3;
    return s->rpc_result;
}

bool t384_frame_source_init(void) { return true; }
void t384_module_files_init(void) { }
void t384_frame_source_task(void)
{
    t384_dualcore_shared_t *s = &t384_dualcore_shared;
    if (__atomic_load_n(&s->v5f_booted, __ATOMIC_ACQUIRE) != 0u &&
        s->v3f_frame_access_ok == 0u) {
        bool valid = true;
        for (unsigned region = 0u; region < T384_FRAME_PROBE_REGIONS; ++region) {
            uint32_t probe;
            memcpy(&probe, t384_frame_probe_region(region), sizeof(probe));
            valid = valid && probe == T384_DTCM_PROBE + region;
        }
        __atomic_store_n(&s->v3f_frame_access_ok,
                         valid ? 1u : 2u, __ATOMIC_RELEASE);
    }
    read_snapshot();
}
const char *t384_frame_source_name(void)
{
#if T384_RAW16_PROFILE == 640u
    return "mini2-dvp-v5f-640-sram-picture-v6";
#else
    return "mini2-dvp-v5f-double-frame-v2";
#endif
}

bool t384_frame_source_stream_ready(void)
{
    read_snapshot();
    return source_snapshot.stream_ready != 0u && !file_snapshot.held;
}

uint16_t t384_frame_source_pixel_format(void)
{
    read_snapshot();
    return (uint16_t)source_snapshot.pixel_format;
}

uint16_t t384_frame_source_mode_flags(void)
{
    read_snapshot();
    return source_snapshot.frame_mode == T384_FRAME_MODE_TPD_Y16
        ? T384_CHUNK_FLAG_TPD_Y16 : T384_CHUNK_FLAG_PICTURE_UYVY;
}

void t384_frame_source_get_stats(t384_frame_source_stats_t *out)
{
    if (!out) return;
    read_snapshot();
    *out = source_snapshot;
}

int t384_module_files_start(const char *id, bool stream_active, uint32_t now)
{
    (void)now;
    return request(T384_RPC_FILE_START, 0u, id, stream_active);
}

void t384_module_files_task(uint32_t now) { (void)now; }
void t384_module_files_abort(uint32_t now)
{
    (void)now;
    (void)request(T384_RPC_FILE_ABORT, 0u, NULL, false);
}

const t384_module_file_status_t *t384_module_files_status(void)
{
    read_snapshot();
    return &file_snapshot;
}

bool t384_module_files_busy(void)
{
    read_snapshot();
    return file_snapshot.held;
}

uint8_t *t384_module_files_download(uint32_t transaction)
{
    if (request(T384_RPC_FILE_DOWNLOAD, transaction, NULL, false) != 0)
        return NULL;
    return (uint8_t *)(uintptr_t)t384_dualcore_shared.rpc_pointer;
}

void t384_module_files_download_release(bool complete)
{
    (void)request(T384_RPC_FILE_RELEASE, complete, NULL, false);
}
#endif
