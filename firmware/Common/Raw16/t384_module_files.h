#ifndef T384_MODULE_FILES_H
#define T384_MODULE_FILES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define T384_MODULE_FILE_MAX_BYTES 32768u
#define T384_MODULE_FILE_HEADER_RESERVE 256u
typedef enum {
    T384_MF_IDLE, T384_MF_READING, T384_MF_READY,
    T384_MF_DONE, T384_MF_ERROR, T384_MF_ABORTED
} t384_module_file_state_t;
typedef struct {
    t384_module_file_state_t state;
    uint32_t transaction, length, received, crc32;
    int error; /* local error; never masquerades as an OEM SDK error */
    uint8_t uart_status, open_status, close_status, command;
    uint8_t error_command, error_uart_status;
    bool held, downloading, cleanup_failed;
    char id[16], path[96], pn[33], sn[33];
    uint8_t fw[11];
} t384_module_file_status_t;

/* Board adapter: all functions except the RX ISR run in the main task.
 * pause must stop DMA/ISR and acquire the exclusive pipeline scratch lease. */
bool t384_module_file_port_pause(uint8_t **scratch, size_t *capacity);
void t384_module_file_port_resume(void);
bool t384_module_file_port_tx(uint8_t byte);
int t384_module_file_port_rx(void); /* -1 empty, -2 hardware/queue error */

int t384_module_files_start(const char *id, bool stream_active, uint32_t now);
void t384_module_files_task(uint32_t now);
void t384_module_files_abort(uint32_t now);
const t384_module_file_status_t *t384_module_files_status(void);
bool t384_module_files_busy(void);
uint8_t *t384_module_files_download(uint32_t transaction);
void t384_module_files_download_release(bool complete);

#endif
