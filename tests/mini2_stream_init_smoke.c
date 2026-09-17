/* Actual boot control code, fake UART/clock only: no device I/O or Flash. */
#include <assert.h>
#include <stdio.h>
#include "ch32h417.h"
/* Discarded hardware-only functions still assemble before linker GC.  Stub
 * the vendor RISC-V fence here, never in the product interrupt code. */
#define NVIC_DisableIRQ(irq) ((void)(irq))
#include "../firmware/Common/Raw16/t384_frame_source_mini2.c"

enum scenario {
    NORMAL, DELAYED_ACK, LOST_DVP_ACK, LOST_TPD_ACK, TPD_REJECT,
    WRONG_FPS, WRONG_FORMAT, MODE_MISMATCH, MODE_QUERY_TIMEOUT,
    DETECTOR_TIMEOUT, DETECTOR_SLOW_RECOVERY, SILENT,
    STALE_ACK, NOISE, CORRUPT_REPLY, FIRST_QUERY_TIMEOUT,
    MODE_RESETS_OUTPUT, TRUNCATED_PREFIX, BAD_THEN_GOOD,
    DELAYED_DIGITAL_STATE, DIGITAL_STAYS_OFF, SCENARIO_COUNT
};
static enum scenario scenario;
static uint32_t now, reply_at, detector_ready_at, digital_ready_at;
static uint8_t command[23], reply[96], detector, digital[3], mode;
static size_t command_length, reply_length, reply_offset;
static unsigned detector_sets, mode_sets, persist_sets, digital_queries;

uint32_t t384_millis(void) { return now++; }

static void append_response(uint8_t status, const uint8_t *data, size_t length)
{
    const size_t start = reply_length;
    assert(start + length + 9u <= sizeof(reply));
    reply[reply_length++] = 0xBEu;
    reply[reply_length++] = 0xAAu;
    reply[reply_length++] = (uint8_t)(length + 1u);
    reply[reply_length++] = 0u;
    reply[reply_length++] = status;
    if (length != 0u) {
        memcpy(reply + reply_length, data, length);
        reply_length += length;
    }
    const uint16_t crc = t384_mini2_crc16_xmodem(reply + start,
                                               reply_length - start);
    reply[reply_length++] = (uint8_t)crc;
    reply[reply_length++] = (uint8_t)(crc >> 8);
    reply[reply_length++] = 0xEBu;
    reply[reply_length++] = 0xAAu;
}

static void respond(void)
{
    assert(command_length == sizeof(command));
    assert(command[0] == 0x55u &&
           (command[5] == 0x10u || command[5] == 0x01u));
    assert(t384_mini2_crc16_xmodem(command + 5u, 16u) ==
           ((uint16_t)command[21] | ((uint16_t)command[22] << 8)));
    const uint8_t index = command[7];
    reply_offset = reply_length = 0u;
    reply_at = now;
    if (scenario == NOISE) {
        static const uint8_t noise[] = {0x00u, 0xBEu, 0x00u, 0xAAu};
        memcpy(reply, noise, sizeof(noise));
        reply_length = sizeof(noise);
    }
    if (scenario == TRUNCATED_PREFIX) {
        static const uint8_t partial[] = {0xBEu, 0xAAu, 0x03u, 0x00u};
        memcpy(reply, partial, sizeof(partial));
        reply_length = sizeof(partial);
    }
    if (scenario == BAD_THEN_GOOD) {
        append_response(0u, NULL, 0u);
        reply[5] ^= 1u;
    }
    if (index == 0x49u) ++persist_sets;
    if (index == 0x44u) {
        ++detector_sets;
        assert(command[9] == T384_MINI2_DETECTOR_FPS);
        if (scenario == DETECTOR_TIMEOUT) return;
        detector = command[9];
        if (scenario == DETECTOR_SLOW_RECOVERY) {
            detector_ready_at = now + T384_MINI2_DETECTOR_SET_TIMEOUT_MS + 300u;
            return; /* State applies, but UART is not available yet. */
        }
    } else if (index == 0x46u) {
        memcpy(digital, command + 9u, 3u);
        if (digital[0] == 1u) {
            assert(digital[2] == T384_MINI2_DVP_FPS);
            if (scenario == WRONG_FPS) digital[2] = 17u;
            if (scenario == WRONG_FORMAT) digital[1] = 0u;
            if (scenario == LOST_DVP_ACK) return;
            if (scenario == DELAYED_DIGITAL_STATE) digital_ready_at = now + 500u;
        }
    } else if (index == 0x45u) {
        ++mode_sets;
        if (scenario == TPD_REJECT && command[9] == 1u) {
            append_response(1u, NULL, 0u);
            return;
        }
        if (scenario != MODE_MISMATCH) mode = command[9];
        if (scenario == MODE_RESETS_OUTPUT) digital[0] = 0u;
        if (scenario == LOST_TPD_ACK && command[9] == 1u) return;
    }
    if (scenario == SILENT ||
        (scenario == DETECTOR_TIMEOUT && detector_sets != 0u) ||
        now < detector_ready_at) return;
    if (index == 0x81u && command[5] == 0x01u) {
        static const uint8_t name[] = "WN2384";
        if (scenario == STALE_ACK) append_response(0u, NULL, 0u);
        append_response(0u, name, sizeof(name));
    } else if (index == 0x84u) {
        append_response(0u, &detector, 1u);
    } else if (index == 0x86u) {
        /* Independent OEM SDK contract; the old request of four is wrong. */
        assert(command[17] == 3u);
        ++digital_queries;
        if (scenario == FIRST_QUERY_TIMEOUT && digital_queries == 1u) return;
        if (scenario == STALE_ACK) append_response(0u, NULL, 0u);
        const uint8_t disabled[3] = {0u};
        append_response(0u, (scenario == DIGITAL_STAYS_OFF ||
                            now < digital_ready_at) ? disabled : digital,
                        sizeof(digital));
    } else if (index == 0x85u) {
        if (scenario == MODE_QUERY_TIMEOUT) return;
        append_response(0u, &mode, 1u);
    } else {
        append_response(0u, NULL, 0u);
        if (scenario == DELAYED_ACK) reply_at += 400u;
    }
    if (scenario == CORRUPT_REPLY && reply_length >= 9u) {
        reply[reply_length - 4u] ^= 1u;
    }
}

FlagStatus USART_GetFlagStatus(USART_TypeDef *usart, uint16_t flag)
{
    (void)usart;
    if (flag == USART_FLAG_RXNE)
        return now >= reply_at && reply_offset < reply_length ? SET : RESET;
    return SET;
}
void USART_SendData(USART_TypeDef *usart, uint16_t data)
{
    (void)usart;
    assert(command_length < sizeof(command));
    command[command_length++] = (uint8_t)data;
    if (command_length == sizeof(command)) {
        respond();
        command_length = 0u;
    }
}
uint16_t USART_ReceiveData(USART_TypeDef *usart)
{
    (void)usart;
    assert(reply_offset < reply_length);
    return reply[reply_offset++];
}

int main(void)
{
    for (scenario = NORMAL; scenario < SCENARIO_COUNT; ++scenario) {
        now = reply_at = detector_ready_at = digital_ready_at = 0u;
        command_length = reply_length = reply_offset = 0u;
        detector_sets = mode_sets = persist_sets = digital_queries = 0u;
        memset((void *)&source_stats, 0, sizeof(source_stats));
        source_stats.frame_mode = T384_FRAME_MODE_UNKNOWN;
        source_stats.mini2_query_detector_valid = 1u;
        detector = T384_MINI2_DETECTOR_FPS;
        if (scenario == DETECTOR_TIMEOUT || scenario == DETECTOR_SLOW_RECOVERY)
            detector = T384_MINI2_DETECTOR_FPS == 60u ? 30u : 25u;
        source_stats.mini2_query_detector_fps = detector;
        memset(digital, 0, sizeof(digital));
        mode = scenario == MODE_MISMATCH ? 3u : 0u;
        mini2_configure_stream();
        assert(persist_sets == 0u);
        assert(now < 20000u);
        const bool ready = scenario == NORMAL || scenario == DELAYED_ACK ||
                           scenario == LOST_DVP_ACK || scenario == LOST_TPD_ACK ||
                           scenario == TPD_REJECT ||
                           scenario == DETECTOR_SLOW_RECOVERY ||
                           scenario == STALE_ACK || scenario == NOISE ||
                           scenario == FIRST_QUERY_TIMEOUT ||
                           scenario == MODE_RESETS_OUTPUT ||
                           scenario == TRUNCATED_PREFIX ||
                           scenario == BAD_THEN_GOOD ||
                           scenario == DELAYED_DIGITAL_STATE;
        assert((source_stats.stream_ready != 0u) == ready);
        if (ready) {
            assert(source_stats.mini2_query_digital_valid == 1u);
            assert(source_stats.mini2_query_digital_enabled == 1u);
            assert(source_stats.mini2_query_digital_format == 1u);
            assert(source_stats.mini2_query_digital_fps == T384_MINI2_DVP_FPS);
            const bool picture = scenario == TPD_REJECT ||
                                 scenario == MODE_RESETS_OUTPUT;
            assert(source_stats.frame_mode == (picture
                       ? T384_FRAME_MODE_PICTURE : T384_FRAME_MODE_TPD_Y16));
            assert(source_stats.pixel_format == (picture
                       ? T384_FRAME_PIXEL_FORMAT_UYVY
                       : T384_FRAME_PIXEL_FORMAT_Y16_BE));
        } else {
            assert(source_stats.frame_mode == T384_FRAME_MODE_UNKNOWN);
            assert(source_stats.pixel_format == 0u);
        }
        if (scenario == DETECTOR_TIMEOUT) {
            assert(detector_sets == 1u && mode_sets == 0u);
            assert(source_stats.mini2_control_ack_valid == 0u);
            assert(source_stats.mini2_control_ack_status == 0xFFu);
        } else if (scenario != DETECTOR_SLOW_RECOVERY) {
            assert(detector_sets == 0u);
            assert(source_stats.mini2_control_detector30_status ==
                   T384_MINI2_CONTROL_SKIPPED);
        }
        if (scenario == MODE_QUERY_TIMEOUT) {
            assert(mode_sets == 1u);
            assert(source_stats.mini2_control_picture_fallback_used == 0u);
        }
        if (scenario == DELAYED_DIGITAL_STATE) {
            assert(now >= digital_ready_at && digital_queries > 3u);
        }
        if (scenario == DIGITAL_STAYS_OFF) {
            assert(now >= T384_MINI2_STATE_CONFIRM_TIMEOUT_MS);
            assert(mode_sets == 0u && source_stats.stream_ready == 0u);
        }
        printf("MINI2 init profile=%u scenario=%u passed\n",
               T384_RAW16_PROFILE, (unsigned)scenario);
    }
    for (unsigned attempt = 0u; attempt < 2u; ++attempt) {
        scenario = attempt == 0u ? NORMAL : STALE_ACK;
        uint8_t name[32] = {0};
        uint16_t length = 99u;
        assert(mini2_uart0_query_info(0x06u, sizeof(name), name, &length) == 1);
        assert(length == 7u && strcmp((const char *)name, "WN2384") == 0);
    }
    uint8_t digital_data[3] = {0u};
    uint16_t digital_length = 0u;
    t384_mini2_build_digital_query_command(command);
    scenario = NORMAL;
    assert(mini2_uart0_send_command(command, digital_data, &digital_length,
                                   sizeof(digital_data)) == 1);
    assert(digital_length == 3u);
#if T384_RAW16_PROFILE == 384u
    for (unsigned test = 0u; test < 7u; ++test) {
        scenario = test == 2u ? SILENT : test == 3u ? MODE_MISMATCH :
                   test == 4u ? LOST_DVP_ACK : test == 5u ? WRONG_FORMAT :
                   test == 6u ? WRONG_FPS : NORMAL;
        now = reply_at = detector_ready_at = digital_ready_at = 0u;
        command_length = reply_length = reply_offset = 0u;
        detector_sets = mode_sets = persist_sets = digital_queries = 0u;
        memset((void *)&source_stats, 0, sizeof(source_stats));
        source_stats.frame_mode = T384_FRAME_MODE_TPD_Y16;
        source_stats.stream_ready = 1u;
        module_rearm_pending = false;
        last_module_rearm_ms = 0u;
        mode = test == 3u ? 3u : T384_MINI2_STREAM_MODE_TPD_Y16;
        digital[0] = test == 1u ? 1u : 0u;
        digital[1] = 1u;
        digital[2] = T384_MINI2_DVP_FPS;
        mini2_probe_and_rearm();
        assert(source_stats.stream_ready == 0u);
        assert(source_stats.dvp_module_probes == 1u);
        assert(detector_sets == 0u && mode_sets == 0u && persist_sets == 0u);
        if (test == 2u || test == 3u) {
            assert(source_stats.dvp_module_rearms == 0u);
        } else {
            assert(source_stats.dvp_module_rearms == 1u && module_rearm_pending);
            mini2_probe_and_rearm();
            assert(source_stats.dvp_module_probes == 2u);
            const bool valid = test != 5u && test != 6u;
            assert((source_stats.stream_ready != 0u) == valid);
            assert(source_stats.dvp_module_rearms == 1u);
            if (!valid) {
                assert(now < 2500u);
                now += T384_MINI2_MODULE_REARM_MS;
                mini2_probe_and_rearm();
                assert(source_stats.dvp_module_rearms == 2u);
            }
        }
        assert(now < 7500u);
        printf("MINI2 live module rearm/read-back scenario=%u passed\n", test);
    }
#endif
    puts("MINI2 native detector/output split, ACK recovery and fail-closed passed");
    return 0;
}
