#include <assert.h>
#include <stdio.h>
#include "t384_dns.h"

int main(void)
{
    const uint8_t query[] = {0x12,0x34,1,0,0,1,0,0,0,0,0,0,
        2,'i','r',6,'s','i','p','e','e','d',3,'c','o','m',0,0,1,0,1};
    uint8_t packet[160]; const uint8_t ip[] = {192,168,17,1};
    memcpy(packet, query, sizeof(query));
    size_t n = t384_dns_reply(packet, sizeof(query), sizeof(packet), ip);
    assert(n == sizeof(query) + 16u && packet[7] == 1u && packet[3] == 0u);
    assert(packet[0] == 0x12 && packet[1] == 0x34 && (packet[2] & 1u));
    assert(memcmp(packet + n - 4u, ip, 4u) == 0);
    memcpy(packet, query, sizeof(query)); packet[11] = 1;
    const uint8_t opt[] = {0,0,41,4,208,0,0,0,0,0,0};
    memcpy(packet + sizeof(query), opt, sizeof(opt));
    assert(t384_dns_reply(packet, sizeof(query) + sizeof(opt), sizeof(packet), ip) == n);
    assert(packet[11] == 0u);
    memcpy(packet, query, sizeof(query)); packet[13] = 'I'; packet[14] = 'R';
    assert(t384_dns_reply(packet, sizeof(query), sizeof(packet), ip) == n);
    memcpy(packet, query, sizeof(query)); packet[13] = 'x';
    assert(t384_dns_reply(packet, sizeof(query), sizeof(packet), ip) == sizeof(query));
    assert(packet[3] == 3u && packet[7] == 0u);
    memcpy(packet, query, sizeof(query)); packet[sizeof(query) - 3u] = 28; /* AAAA: NODATA */
    assert(t384_dns_reply(packet, sizeof(query), sizeof(packet), ip) == sizeof(query));
    assert(packet[3] == 0u && packet[7] == 0u);
    for (size_t len = 0u; len < sizeof(query); ++len) {
        memcpy(packet, query, sizeof(query)); assert(t384_dns_reply(packet, len, sizeof(packet), ip) == 0u);
    }
    memcpy(packet, query, sizeof(query)); packet[12] = 0xc0;
    assert(t384_dns_reply(packet, sizeof(query), sizeof(packet), ip) == 0u);
    memcpy(packet, query, sizeof(query));
    assert(t384_dns_reply(packet, sizeof(query), sizeof(query), ip) == 0u);
    puts("local DNS A/case/AAAA/NXDOMAIN/truncation/bounds smoke passed");
}
