#ifndef T384_MINI2_PROTOCOL_H
#define T384_MINI2_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define T384_MINI2_DVP30_COMMAND_BYTES 23u
#define T384_MINI2_GENERIC_ACK_BYTES 9u
#define T384_MINI2_MAX_RESPONSE_BYTES 48u

void t384_mini2_build_video_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES],
    uint8_t command_index,
    uint8_t status,
    uint8_t format,
    uint8_t fps);

uint16_t t384_mini2_crc16_xmodem(const uint8_t *data, size_t length);
void t384_mini2_build_dvp30_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES]);
void t384_mini2_build_query_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES],
    uint8_t command_index,
    uint8_t response_data_length);
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
