#ifndef T384_MINI2_PROTOCOL_H
#define T384_MINI2_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define T384_MINI2_DVP30_COMMAND_BYTES 23u
#define T384_MINI2_GENERIC_ACK_BYTES 9u
#define T384_MINI2_MAX_RESPONSE_BYTES 48u
/* Current OEM SDK 0x86 getter: status, format, FPS (no fourth padding byte). */
#define T384_MINI2_DIGITAL_RESPONSE_BYTES 3u

#define T384_MINI2_FILE_NAME_BYTES 256u
#define T384_MINI2_FILE_OPEN_BYTES 279u
#define T384_MINI2_FILE_BLOCK_BYTES 512u
/* Only file open (read mode), info, read and close can be constructed. */
bool t384_mini2_build_file_open(uint8_t command[T384_MINI2_FILE_OPEN_BYTES],
                              uint8_t file_id, const char *path);
bool t384_mini2_build_file_read(uint8_t command[23], uint8_t file_id,
                              uint32_t offset, uint16_t length);
void t384_mini2_build_file_info(uint8_t command[23], uint8_t file_id);
void t384_mini2_build_file_close(uint8_t command[23], uint8_t file_id);

#define T384_MINI2_STREAM_MODE_PICTURE 0u
#define T384_MINI2_STREAM_MODE_TPD_Y16 1u

void t384_mini2_build_video_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES],
    uint8_t command_index,
    uint8_t status,
    uint8_t format,
    uint8_t fps);

uint16_t t384_mini2_crc16_xmodem(const uint8_t *data, size_t length);
void t384_mini2_build_dvp30_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES]);
void t384_mini2_build_stream_mode_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES], uint8_t mode);
void t384_mini2_build_query_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES],
    uint8_t command_index,
    uint8_t response_data_length);
void t384_mini2_build_digital_query_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES]);
void t384_mini2_build_class_query_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES],
    uint8_t command_class,
    uint8_t command_index,
    uint8_t parameter,
    uint8_t response_data_length);
void t384_mini2_build_vtemp_query_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES]);
void t384_mini2_build_info_query_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES],
    uint8_t subcommand,
    uint8_t response_data_length);
bool t384_mini2_parse_response(const uint8_t *response,
                               size_t received_length,
                               uint8_t *status,
                               const uint8_t **data,
                               uint16_t *data_length);
bool t384_mini2_parse_generic_ack(
    const uint8_t response[T384_MINI2_GENERIC_ACK_BYTES],
    uint8_t *status);

#endif
