#include <assert.h>
#include <stdio.h>
#include "dhserver.h"
#include "t384_product_config.h"

extern int fill_options(void *, uint8_t, const char *, ip4_addr_t, int,
                         ip4_addr_t, ip4_addr_t, ip4_addr_t);

int main(void)
{
    uint8_t packet[128]; ip4_addr_t device, subnet;
    IP4_ADDR(&device, T384_NCM_IPV4_A, T384_NCM_IPV4_B, T384_NCM_IPV4_C, T384_NCM_DEVICE_HOST);
    IP4_ADDR(&subnet, 255,255,255,0);
    assert(T384_NCM_IPV4_C == 17u && T384_NCM_CLIENT_COUNT == 19u);
    for (unsigned type = 2u; type <= 5u; type += 3u) { /* OFFER and ACK */
        const int length = fill_options(packet, (uint8_t)type, NULL, device, 86400, device, device, subnet);
        int router = 0, dns = 0, identifier = 0;
        for (int i = 0; i < length && packet[i] != 255u;) {
            assert(i + 1 < length); const uint8_t id = packet[i], count = packet[i + 1];
            assert(i + 2 + count <= length);
            if (id == 3u || id == 6u || id == 54u) {
                const uint8_t expected[] = {192,168,17,1};
                assert(count == 4u && memcmp(packet + i + 2, expected, 4u) == 0);
                router += id == 3u; dns += id == 6u; identifier += id == 54u;
            }
            i += 2 + count;
        }
        assert(router == 1 && dns == 1 && identifier == 1);
    }
    puts("DHCP OFFER/ACK device IP/server-id/router/DNS option bytes passed");
}
