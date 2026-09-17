/* Real calibration handlers with the existing fake TCP and RAM Flash. */
#define main calibration_storage_fixture_main
#include "t384_calibration_storage_smoke.c"
#undef main
#define T384_HTTP_FIXTURE_NO_MAIN
#include "module_files_http_smoke.c"

int main(void)
{
    assert(calibration_storage_fixture_main() == 0);
    t384_cal_manifest_t manifest;
    assert(t384_cal_storage_manifest(&manifest) == T384_CAL_OK);
    strcpy(manifest.profile, "384x288");
    manifest.payload_len = T384_CAL_STORAGE_MAX_PAYLOAD;
    uint8_t payload[T384_CAL_STORAGE_MAX_PAYLOAD];
    for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)(i * 17u);
    assert(t384_cal_storage_begin(&manifest) == T384_CAL_OK);
    assert(t384_cal_storage_init() == T384_CAL_BUSY);
    assert(t384_cal_storage_write(0u, payload, sizeof(payload)) == T384_CAL_OK);
    assert(t384_cal_storage_finish() == T384_CAL_OK);
    assert(t384_cal_storage_init() == T384_CAL_OK);

    struct tcp_pcb pcb = {0};
    http_client_t *client = &clients[0];
    client_request(client, &pcb, "GET /api/v1/device HTTP/1.1\r\n\r\n");
    assert(handle_calibration_request(client) == ERR_OK);
    assert(memcmp(transmitted, "HTTP/1.0 200", 12u) == 0);
    transmitted[transmitted_length] = 0;
    assert(strstr((const char *)transmitted, "\"device_ip\":\"192.168.17.1\"") != NULL);
    assert(strstr((const char *)transmitted, "\"ota\":{\"supported\":false}") != NULL);

    client_request(client, &pcb, "PUT /api/v1/calibration/v1/data HTTP/1.1\r\n\r\n");
    int header_length = snprintf((char *)client->request, sizeof(client->request),
                                 "PUT /api/v1/calibration/v1/data HTTP/1.1\r\n"
                                 "Content-Length: %lu\r\n\r\n",
                                 (unsigned long)(sizeof(manifest) + sizeof(payload)));
    assert(header_length > 0);
    assert((size_t)header_length + sizeof(manifest) + sizeof(payload)
           < sizeof(client->request));
    memcpy(client->request + header_length, &manifest, sizeof(manifest));
    memcpy(client->request + header_length + sizeof(manifest), payload, sizeof(payload));
    client->request_length = (size_t)header_length + sizeof(manifest) + sizeof(payload);
    client->request[client->request_length] = 0;
    assert(handle_calibration_request(client) == ERR_OK);
    assert(memcmp(transmitted, "HTTP/1.0 200", 12u) == 0);
    assert(t384_cal_storage_init() == T384_CAL_BUSY);

    client_request(client, &pcb, "POST /api/v1/calibration/v1/commit HTTP/1.1\r\n\r\n");
    assert(handle_calibration_request(client) == ERR_OK);
    assert(memcmp(transmitted, "HTTP/1.0 200", 12u) == 0);
    assert(t384_cal_storage_init() == T384_CAL_OK);

    client_request(client, &pcb, "GET /api/v1/calibration/v1/data HTTP/1.1\r\n\r\n");
    assert(handle_calibration_request(client) == ERR_OK);
    ack_all(client);
    assert(memcmp(transmitted, "HTTP/1.0 200", 12u) == 0);
    const uint8_t *body = (const uint8_t *)strstr((const char *)transmitted, "\r\n\r\n") + 4u;
    assert(transmitted_length - (size_t)(body - transmitted) == sizeof(payload));
    assert(memcmp(body, payload, sizeof(payload)) == 0);

    client_request(client, &pcb, "GET /api/v1/calibration/v1/manifest HTTP/1.1\r\n\r\n");
    assert(handle_calibration_request(client) == ERR_OK);
    ack_all(client);
    transmitted[transmitted_length] = 0;
    assert(memcmp(transmitted, "HTTP/1.0 200", 12u) == 0);
    assert(strstr((const char *)transmitted, "\"profile\":\"384x288\"") != NULL);
    assert(strstr((const char *)transmitted, "\"payload_len\":2048") != NULL);

    manifest.payload_len = T384_CAL_STORAGE_MAX_PAYLOAD + 1u;
    assert(t384_cal_storage_begin(&manifest) == T384_CAL_FORMAT);
    puts("384 calibration max-payload HTTP upload/commit/manifest/boot-readback smoke passed");
    return 0;
}
