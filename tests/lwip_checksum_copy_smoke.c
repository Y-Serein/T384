#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lwip/inet_chksum.h"

uint16_t lwip_standard_chksum(const void *data, int length);

static void fill_pattern(uint8_t *data, size_t length, uint32_t seed)
{
    for (size_t i = 0u; i < length; ++i) {
        data[i] = (uint8_t)(seed + i * 37u + (i >> 3));
    }
}

int main(void)
{
    uint8_t source_storage[8192u + 16u];
    uint8_t expected_storage[8192u + 16u];
    uint8_t actual_storage[8192u + 16u];
    fill_pattern(source_storage, sizeof(source_storage), 0x31u);

    for (unsigned source_offset = 0u; source_offset < 8u; ++source_offset) {
        for (unsigned destination_offset = 0u;
             destination_offset < 8u; ++destination_offset) {
            for (unsigned length = 0u; length <= 8192u; ++length) {
                if (length > 64u && length != 1460u && length != 6144u &&
                    length != 6180u && length != 8192u &&
                    (length % 127u) != 0u) {
                    continue;
                }
                const uint8_t *source = source_storage + source_offset;
                uint8_t *expected = expected_storage + destination_offset;
                uint8_t *actual = actual_storage + destination_offset;
                memcpy(expected, source, length);
                memset(actual, 0xa5, length + 8u);

                const uint16_t expected_sum =
                    lwip_standard_chksum(source, (int)length);
                const uint16_t actual_sum =
                    t384_lwip_chksum_copy(actual, source, (uint16_t)length);
                assert(expected_sum == actual_sum);
                assert(memcmp(expected, actual, length) == 0);
                for (unsigned guard = 0u; guard < 8u; ++guard) {
                    assert(actual[length + guard] == 0xa5u);
                }
            }
        }
    }

    puts("T384 fused checksum-copy smoke passed");
    return 0;
}
