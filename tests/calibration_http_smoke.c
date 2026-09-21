/* Real calibration handlers with the existing fake TCP and RAM Flash. */
#ifndef T384_RAW16_PROFILE
#define T384_RAW16_PROFILE 384u
#endif
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
    seal_manifest(&manifest,payload);
    assert(t384_cal_storage_begin(&manifest) == T384_CAL_OK);
    assert(t384_cal_storage_init() == T384_CAL_BUSY);
    assert(t384_cal_storage_write(0u, payload, sizeof(payload)) == T384_CAL_OK);
    assert(t384_cal_storage_finish() == T384_CAL_OK);
    assert(t384_cal_storage_init() == T384_CAL_OK);

    t384_temp_model_config_t config;
    get_temp_model_config(&config);
    assert(strcmp(config.model, T384_EXPERIMENTAL_TEMP_MODEL) == 0);
    /* A CRC-valid arbitrary payload is stored but cannot enable temperatures. */
    t384_cal_empirical_2point_t temperature = {0, 50, 29670, 34400, 94600};
    t384_cal_manifest_t temperature_manifest = manifest;
    temperature_manifest.payload_len = sizeof(temperature);
    seal_manifest(&temperature_manifest, (const uint8_t *)&temperature);
    assert(t384_cal_storage_begin(&temperature_manifest) == T384_CAL_OK);
    assert(t384_cal_storage_write(0, &temperature, sizeof(temperature)) == T384_CAL_OK);
    assert(t384_cal_storage_finish() == T384_CAL_OK);
    get_temp_model_config(&config);
    assert(strcmp(config.model, T384_CAL_MODEL_EMPIRICAL_2POINT) == 0);
    assert(config.zero_c_x100 == 2967000ul && config.counts_per_c_x100 == 9460ul);
    assert(t384_cal_storage_init() == T384_CAL_OK);
    get_temp_model_config(&config);
    assert(config.zero_c_x100 == 2967000ul && config.counts_per_c_x100 == 9460ul);
    /* Restore the max-payload fixture used by the API cases below. */
    assert(t384_cal_storage_begin(&manifest) == T384_CAL_OK);
    assert(t384_cal_storage_write(0, payload, sizeof(payload)) == T384_CAL_OK);
    assert(t384_cal_storage_finish() == T384_CAL_OK);

    /* A corrupted upload must not occupy staging or reach Flash. */
    uint8_t packet[sizeof(manifest)+sizeof(payload)];
    memcpy(packet,&manifest,sizeof(manifest));
    memcpy(packet+sizeof(manifest),payload,sizeof(payload));
    packet[sizeof(packet)-1u]^=1u;
    assert(storage_put_packet(packet,sizeof(packet))==T384_CAL_CRC);
    assert(t384_cal_storage_init()==T384_CAL_OK);

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
    /* Fragmented bodies wait without changing staging; header names are
     * case-insensitive. */
    memcpy(client->request+strlen("PUT /api/v1/calibration/v1/data HTTP/1.1\r\n"),
           "content-length:",15u);
    client->request_length-=1u;
    assert(handle_calibration_request(client)==ERR_OK && transmitted_length==0u);
    assert(t384_cal_storage_init()==T384_CAL_OK);
    ++client->request_length;
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
    assert(strstr((const char *)transmitted, "\"applied\":false") != NULL);
    assert(strstr((const char *)transmitted, "\"oem_radiometry_ready\":false") != NULL);

    /* Flash commit cannot interrupt a live stream or module transaction. */
    assert(t384_cal_storage_begin(&manifest)==T384_CAL_OK);
    assert(t384_cal_storage_write(0u,payload,sizeof(payload))==T384_CAL_OK);
    raw16_client=&clients[1];
    client_request(client,&pcb,"POST /api/v1/calibration/v1/commit HTTP/1.1\r\n\r\n");
    assert(handle_calibration_request(client)==ERR_OK);
    assert(memcmp(transmitted,"HTTP/1.0 409",12u)==0);
    ack_all(client);
    raw16_client=NULL;
    assert(t384_cal_storage_init()==T384_CAL_BUSY);
    client_request(client,&pcb,"POST /api/v1/calibration/v1/commit HTTP/1.1\r\n\r\n");
    assert(handle_calibration_request(client)==ERR_OK);
    assert(memcmp(transmitted,"HTTP/1.0 200",12u)==0);
    ack_all(client);

    /* Exact route matching: a suffix must never commit or abort a package. */
    assert(t384_cal_storage_begin(&manifest)==T384_CAL_OK);
    client_request(client,&pcb,"POST /api/v1/calibration/v1/abortXYZ HTTP/1.1\r\n\r\n");
    assert(handle_calibration_request(client)==ERR_OK);
    assert(memcmp(transmitted,"HTTP/1.0 404",12u)==0);
    ack_all(client);
    assert(t384_cal_storage_init()==T384_CAL_BUSY);
    t384_cal_storage_abort();

    const char *invalid[]={
        "POST /api/v1/calibration/v1/commit HTTP/1.1\r\nContent-Length: -1\r\n\r\n",
        "POST /api/v1/calibration/v1/commit HTTP/1.1\r\nContent-Length: 0x0\r\n\r\n",
        "POST /api/v1/calibration/v1/commit HTTP/1.1\r\nContent-Length: 0\r\ncontent-length: 0\r\n\r\n",
        "POST /api/v1/calibration/v1/commit HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
        "POST /api/v1/calibration/v1/commit HTTP/1.1\r\nContent-Length: 0\r\n\r\nx",
    };
    for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        client_request(client,&pcb,invalid[i]);
        assert(handle_calibration_request(client)==ERR_OK);
        assert(memcmp(transmitted,"HTTP/1.0 400",12u)==0);
        ack_all(client);
    }
    /* Binary API responses use the bounded response buffer and one copied
     * TCP response, as in the pre-regression HTTP implementation. */
    client_request(client,&pcb,"GET /api/v1/calibration/v1/data HTTP/1.1\r\n\r\n");
    backpressure=false;
    assert(handle_calibration_request(client)==ERR_OK);
    assert(client->pcb == NULL);
    body=(const uint8_t *)strstr((const char *)transmitted,"\r\n\r\n")+4u;
    assert(transmitted_length-(size_t)(body-transmitted)==sizeof(payload));
    assert(memcmp(body,payload,sizeof(payload))==0);

    manifest.payload_len = T384_CAL_STORAGE_MAX_PAYLOAD + 1u;
    assert(t384_cal_storage_begin(&manifest) == T384_CAL_FORMAT);
    puts("384 calibration max-payload HTTP upload/commit/manifest/boot-readback smoke passed");
    return 0;
}
