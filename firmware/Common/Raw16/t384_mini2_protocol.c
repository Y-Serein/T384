#include "t384_mini2_protocol.h"

#include <string.h>

uint16_t t384_mini2_crc16_xmodem(const uint8_t *data, size_t length)
{
    uint16_t crc = 0u;
    for (size_t index = 0u; index < length; ++index) {
        crc ^= (uint16_t)data[index] << 8;
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u
                      ? (uint16_t)((crc << 1) ^ 0x1021u)
                      : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void t384_mini2_build_video_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES],
    uint8_t command_index,
    uint8_t status,
    uint8_t format,
    uint8_t fps)
{
    uint8_t prefix[] = {
        0x55u, 0x43u, 0x49u, 0x12u, 0x00u,
        0x10u, 0x10u, command_index, 0x00u,
        status, format, fps, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
    };
    memcpy(command, prefix, sizeof(prefix));
    const uint16_t crc = t384_mini2_crc16_xmodem(
        command + 5u, sizeof(prefix) - 5u);
    command[sizeof(prefix)] = (uint8_t)crc;
    command[sizeof(prefix) + 1u] = (uint8_t)(crc >> 8);
}

void t384_mini2_build_dvp30_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES])
{
    t384_mini2_build_video_command(command, 0x46u, 0x01u, 0x01u, 0x1Eu);
}

void t384_mini2_build_query_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES],
    uint8_t command_index,
    uint8_t response_data_length)
{
    t384_mini2_build_video_command(command, command_index, 0u, 0u, 0u);
    command[17] = response_data_length;
    const uint16_t crc = t384_mini2_crc16_xmodem(command + 5u, 16u);
    command[21] = (uint8_t)crc;
    command[22] = (uint8_t)(crc >> 8);
}

void t384_mini2_build_info_query_command(
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES],
    uint8_t subcommand,
    uint8_t response_data_length)
{
    static const uint8_t prefix[] = {
        0x55u, 0x43u, 0x49u, 0x12u, 0x00u,
        0x01u, 0x01u, 0x81u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
    };
    memcpy(command, prefix, sizeof(prefix));
    command[9] = subcommand;
    command[17] = response_data_length;
    const uint16_t crc = t384_mini2_crc16_xmodem(command + 5u, 16u);
    command[21] = (uint8_t)crc;
    command[22] = (uint8_t)(crc >> 8);
}

bool t384_mini2_parse_response(const uint8_t *response,
                               size_t received_length,
                               uint8_t *status,
                               const uint8_t **data,
                               uint16_t *data_length)
{
    if (response == NULL || status == NULL || data == NULL ||
        data_length == NULL || received_length < T384_MINI2_GENERIC_ACK_BYTES ||
        response[0] != 0xBEu || response[1] != 0xAAu) {
        return false;
    }
    const uint16_t payload_length =
        (uint16_t)response[2] | ((uint16_t)response[3] << 8);
    if (payload_length == 0u ||
        received_length != (size_t)payload_length + 8u ||
        response[received_length - 2u] != 0xEBu ||
        response[received_length - 1u] != 0xAAu) {
        return false;
    }
    const uint16_t expected = t384_mini2_crc16_xmodem(
        response, (size_t)payload_length + 4u);
    const uint16_t received =
        (uint16_t)response[payload_length + 4u] |
        ((uint16_t)response[payload_length + 5u] << 8);
    if (expected != received) {
        return false;
    }
    *status = response[4];
    *data = response + 5u;
    *data_length = (uint16_t)(payload_length - 1u);
    return true;
}

bool t384_mini2_parse_generic_ack(
    const uint8_t response[T384_MINI2_GENERIC_ACK_BYTES],
    uint8_t *status)
{
    const uint8_t *data = NULL;
    uint16_t data_length = 0u;
    if (!t384_mini2_parse_response(response, T384_MINI2_GENERIC_ACK_BYTES,
                                   status, &data, &data_length) ||
        data_length != 0u) {
        return false;
    }
    return true;
}
