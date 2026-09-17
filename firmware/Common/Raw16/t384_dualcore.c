#include "t384_dualcore.h"

#if T384_DUALCORE
#include <string.h>
#include "t384_compiler.h"
#include "t384_time.h"
#include "ch32h417.h"

t384_dualcore_shared_t t384_dualcore_shared
    __attribute__((section(".t384_ipc"), aligned(32)));

#ifdef Core_V5F
uint8_t t384_dualcore_frame[T384_RAW16_FRAME_BYTES]
    __attribute__((section(".t384_frame"), aligned(32)));
uint8_t t384_frame1_itcm[T384_FRAME1_ITCM_BYTES]
    __attribute__((section(".t384_frame1_itcm"), aligned(32)));
uint8_t t384_frame1_dtcm[T384_FRAME1_DTCM_BYTES]
    __attribute__((section(".t384_frame1_dtcm"), aligned(32)));
uint8_t t384_frame1_code[T384_FRAME1_CODE_BYTES]
    __attribute__((section(".t384_frame1_code"), aligned(32)));
uint8_t t384_frame1_data[T384_FRAME1_DATA_BYTES]
    __attribute__((section(".t384_frame1_data"), aligned(32)));
#endif

void t384_dualcore_init(void)
{
#ifdef Core_V3F
    memset(&t384_dualcore_shared, 0, sizeof(t384_dualcore_shared));
    t384_dualcore_shared.version = T384_IPC_VERSION;
    __atomic_store_n(&t384_dualcore_shared.magic, T384_IPC_MAGIC,
                     __ATOMIC_RELEASE);
#endif
}

#ifdef Core_V5F
static void publish_snapshot(void)
{
    t384_dualcore_shared_t *s = &t384_dualcore_shared;
    ++s->snapshot_sequence;
    T384_MEMORY_BARRIER();
    t384_frame_source_get_stats(&s->source);
    memcpy(&s->file, t384_module_files_status(), sizeof(s->file));
    T384_MEMORY_BARRIER();
    ++s->snapshot_sequence;
}

void t384_dualcore_capture_task(void)
{
    static uint32_t handled_request, handled_command;
    static bool cleanup_pending;
    static bool acquired_lease;
    static uint32_t snapshot_ms;
    static bool snapshot_valid;
    t384_dualcore_shared_t *s = &t384_dualcore_shared;
    const uint32_t request = __atomic_load_n(&s->rpc_request, __ATOMIC_ACQUIRE);
    if (request != __atomic_load_n(&s->rpc_ack, __ATOMIC_RELAXED)) {
        handled_request = request;
        handled_command = s->rpc_command;
        cleanup_pending = true;
        acquired_lease = false;
        s->rpc_result = -3;
        s->rpc_pointer = 0u;
        if (__atomic_load_n(&s->rpc_cancel, __ATOMIC_ACQUIRE) == 0u ||
            handled_command == T384_RPC_FILE_ABORT ||
            handled_command == T384_RPC_FILE_RELEASE) {
            switch (s->rpc_command) {
            case T384_RPC_FILE_START:
                s->rpc_result = t384_module_files_start(
                    s->rpc_id, s->rpc_stream_active != 0u, t384_millis());
                break;
            case T384_RPC_FILE_ABORT:
                t384_module_files_abort(t384_millis());
                s->rpc_result = 0;
                break;
            case T384_RPC_FILE_DOWNLOAD:
                s->rpc_pointer = (uint32_t)(uintptr_t)
                    t384_module_files_download(s->rpc_argument);
                s->rpc_result = s->rpc_pointer != 0u ? 0 : -3;
                break;
            case T384_RPC_FILE_RELEASE:
                t384_module_files_download_release(s->rpc_argument != 0u);
                s->rpc_result = 0;
                break;
            default:
                break;
            }
        }
        acquired_lease = (handled_command == T384_RPC_FILE_START &&
                          s->rpc_result == 0) ||
                         (handled_command == T384_RPC_FILE_DOWNLOAD &&
                          s->rpc_pointer != 0u);
        /* A timed-out caller must not leave a scratch/download lease behind. */
        if (__atomic_load_n(&s->rpc_cancel, __ATOMIC_ACQUIRE) != 0u) {
            if (acquired_lease && handled_command == T384_RPC_FILE_DOWNLOAD)
                t384_module_files_download_release(false);
            if (acquired_lease && handled_command == T384_RPC_FILE_START)
                t384_module_files_abort(t384_millis());
            acquired_lease = false;
            s->rpc_pointer = 0u;
            s->rpc_result = -3;
        }
        publish_snapshot();
        snapshot_ms = t384_millis();
        snapshot_valid = true;
        __atomic_store_n(&s->rpc_ack, request, __ATOMIC_RELEASE);
    }
    /* Covers cancellation arriving between the final cancel check and ACK.
     * A new request owns a different sequence and must never be cleaned here. */
    if (cleanup_pending && acquired_lease &&
        __atomic_load_n(&s->rpc_request, __ATOMIC_ACQUIRE) == handled_request &&
        __atomic_load_n(&s->rpc_cancel, __ATOMIC_ACQUIRE) != 0u) {
        if (handled_command == T384_RPC_FILE_DOWNLOAD)
            t384_module_files_download_release(false);
        if (handled_command == T384_RPC_FILE_START)
            t384_module_files_abort(t384_millis());
        cleanup_pending = false;
    }
    t384_frame_source_task();
    const uint32_t now = t384_millis();
    /* Avoid copying hundreds of diagnostic bytes into shared SRAM on every
     * idle loop; RPC still publishes immediately before acknowledging. */
    if (!snapshot_valid || (uint32_t)(now - snapshot_ms) >= 10u) {
        publish_snapshot();
        snapshot_ms = now;
        snapshot_valid = true;
    }
}
#endif
#endif
