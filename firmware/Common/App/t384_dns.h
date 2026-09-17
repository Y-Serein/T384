#ifndef T384_DNS_H
#define T384_DNS_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Bounded, single-question DNS responder. Only the product domain is resolved;
 * unrelated domains get NXDOMAIN rather than pretending to provide Internet. */
static size_t t384_dns_reply(uint8_t *packet, size_t length, size_t capacity,
                             const uint8_t address[4])
{
    static const uint8_t name[] = {2,'i','r',6,'s','i','p','e','e','d',3,'c','o','m',0};
    if (length < 17u || capacity < length || (packet[2] & 0xf8u) != 0u ||
        packet[4] != 0u || packet[5] != 1u || packet[6] || packet[7] ||
        packet[8] || packet[9] || packet[10] || packet[11] > 1u) return 0u;
    size_t cursor = 12u;
    while (cursor < length && packet[cursor]) {
        const uint8_t count = packet[cursor++];
        if (count > 63u || cursor + count >= length) return 0u;
        cursor += count;
    }
    if (cursor >= length || cursor + 5u > length) return 0u;
    ++cursor;
    const size_t question_end = cursor + 4u;
    if (packet[11] == 1u) {
        if (question_end + 11u > length || packet[question_end] != 0u ||
            packet[question_end + 1u] != 0u || packet[question_end + 2u] != 41u) return 0u;
        const size_t extra = ((size_t)packet[question_end + 9u] << 8) | packet[question_end + 10u];
        if (question_end + 11u + extra != length) return 0u;
    } else if (question_end != length) return 0u;
    length = question_end;
    packet[11] = 0u; /* Minimal responder strips EDNS; no large UDP response. */
    int match = cursor - 12u == sizeof(name);
    for (size_t i = 0u; match && i < sizeof(name); ++i) {
        uint8_t value = packet[12u + i];
        if (value >= 'A' && value <= 'Z') value = (uint8_t)(value + ('a' - 'A'));
        if (value != name[i]) match = 0;
    }
    const int type_a = packet[cursor] == 0u && packet[cursor + 1u] == 1u;
    const int internet = packet[cursor + 2u] == 0u && packet[cursor + 3u] == 1u;
    packet[2] = (uint8_t)(0x84u | (packet[2] & 1u)); /* QR, AA, preserve RD; no RA */
    packet[3] = match ? 0u : 3u;
    if (!match || !type_a || !internet) return length;
    if (length + 16u > capacity) return 0u;
    static const uint8_t answer[] = {0xc0,0x0c,0,1,0,1,0,0,0,30,0,4};
    memcpy(packet + length, answer, sizeof(answer));
    memcpy(packet + length + sizeof(answer), address, 4u);
    packet[7] = 1u;
    return length + 16u;
}
#endif
