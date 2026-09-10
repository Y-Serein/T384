#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "t384_mini2_protocol.h"

int main(void)
{
    static const uint8_t expected_command[] = {
        0x55u, 0x43u, 0x49u, 0x12u, 0x00u,
        0x10u, 0x10u, 0x46u, 0x00u,
        0x01u, 0x01u, 0x1Eu, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x06u, 0x3Bu,
    };
    static const uint8_t valid_ack[] = {
        0xBEu, 0xAAu, 0x01u, 0x00u, 0x00u,
        0x7Bu, 0x54u, 0xEBu, 0xAAu,
    };
    static const uint8_t detector_response[] = {
        0xBEu, 0xAAu, 0x02u, 0x00u, 0x00u,
        0x1Eu, 0x52u, 0x09u, 0xEBu, 0xAAu,
    };
    static const uint8_t digital_response[] = {
        0xBEu, 0xAAu, 0x05u, 0x00u, 0x00u,
        0x01u, 0x01u, 0x1Eu, 0x00u,
        0x0Du, 0x37u, 0xEBu, 0xAAu,
    };
    static const uint8_t detector_query[] = {
        0x55u, 0x43u, 0x49u, 0x12u, 0x00u,
        0x10u, 0x10u, 0x84u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x01u, 0x00u, 0x00u, 0x00u,
        0xEFu, 0xB1u,
    };
    static const uint8_t digital_query[] = {
        0x55u, 0x43u, 0x49u, 0x12u, 0x00u,
        0x10u, 0x10u, 0x86u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x04u, 0x00u, 0x00u, 0x00u,
        0x68u, 0xFBu,
    };
    uint8_t command[T384_MINI2_DVP30_COMMAND_BYTES];
    uint8_t status = 0xFFu;

    t384_mini2_build_dvp30_command(command);
    if (memcmp(command, expected_command, sizeof(command)) != 0) {
        return 1;
    }
    if (!t384_mini2_parse_generic_ack(valid_ack, &status) || status != 0u) {
        return 2;
    }
    uint8_t corrupt_ack[T384_MINI2_GENERIC_ACK_BYTES];
    memcpy(corrupt_ack, valid_ack, sizeof(corrupt_ack));
    corrupt_ack[5] ^= 1u;
    if (t384_mini2_parse_generic_ack(corrupt_ack, &status)) {
        return 3;
    }
    const uint8_t *data = NULL;
    uint16_t data_length = 0u;
    if (!t384_mini2_parse_response(detector_response,
                                   sizeof(detector_response), &status, &data,
                                   &data_length) || status != 0u ||
        data_length != 1u || data[0] != 0x1Eu) {
        return 6;
    }
    if (!t384_mini2_parse_response(digital_response, sizeof(digital_response),
                                   &status, &data, &data_length) || status != 0u ||
        data_length != 4u || data[0] != 1u || data[1] != 1u ||
        data[2] != 0x1Eu) {
        return 7;
    }
    t384_mini2_build_query_command(command, 0x84u, 1u);
    if (memcmp(command, detector_query, sizeof(command)) != 0) {
        return 8;
    }
    t384_mini2_build_query_command(command, 0x86u, 4u);
    if (memcmp(command, digital_query, sizeof(command)) != 0) {
        return 9;
    }
    t384_mini2_build_info_query_command(command, 0x01u, 32u);
    if (command[5] != 0x01u || command[6] != 0x01u ||
        command[7] != 0x81u || command[9] != 0x01u ||
        command[17] != 32u ||
        t384_mini2_crc16_xmodem(command + 5u, 16u) !=
            ((uint16_t)command[21] | ((uint16_t)command[22] << 8))) {
        return 10;
    }
    t384_mini2_build_info_query_command(command, 0x02u, 11u);
    if (command[9] != 0x02u || command[17] != 11u ||
        t384_mini2_crc16_xmodem(command + 5u, 16u) !=
            ((uint16_t)command[21] | ((uint16_t)command[22] << 8))) {
        return 11;
    }
    t384_mini2_build_video_command(command, 0x46u, 0x00u, 0x00u, 0x00u);
    if (command[7] != 0x46u || command[9] != 0x00u ||
        command[10] != 0x00u || command[11] != 0x00u ||
        t384_mini2_crc16_xmodem(command + 5u, 16u) !=
            ((uint16_t)command[21] | ((uint16_t)command[22] << 8))) {
        return 4;
    }
    t384_mini2_build_video_command(command, 0x4Au, 0x00u, 0x00u, 0x00u);
    if (command[7] != 0x4Au || command[9] != 0x00u ||
        t384_mini2_crc16_xmodem(command + 5u, 16u) !=
            ((uint16_t)command[21] | ((uint16_t)command[22] << 8))) {
        return 5;
    }
    puts("T384 MINI2 UART command/ACK vectors passed");
    return 0;
}
