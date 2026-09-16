/* Exercise actual HTTP handlers and ACK ownership with a fake TCP transport. */
#define main transaction_fixture_main
#include "module_files_smoke.c"
#undef main
#include "../firmware/Common/App/http_status.c"

static uint8_t transmitted[40000];
static size_t transmitted_length;
static bool backpressure;

uint32_t t384_millis(void) { return now; }
err_t tcp_write(struct tcp_pcb *pcb, const void *data, u16_t length, u8_t flags)
{
    (void)pcb;
    assert(flags & TCP_WRITE_FLAG_COPY);
    if (backpressure) return ERR_MEM;
    assert(transmitted_length + length <= sizeof(transmitted));
    memcpy(transmitted+transmitted_length, data, length);
    transmitted_length += length;
    return ERR_OK;
}
err_t tcp_output(struct tcp_pcb *pcb) { (void)pcb; return ERR_OK; }
err_t tcp_close(struct tcp_pcb *pcb) { (void)pcb; return ERR_OK; }
void tcp_abort(struct tcp_pcb *pcb) { (void)pcb; }
void tcp_arg(struct tcp_pcb *pcb, void *arg) { (void)pcb; (void)arg; }
void tcp_recv(struct tcp_pcb *pcb, tcp_recv_fn fn) { (void)pcb; (void)fn; }
void tcp_sent(struct tcp_pcb *pcb, tcp_sent_fn fn) { (void)pcb; (void)fn; }
void tcp_poll(struct tcp_pcb *pcb, tcp_poll_fn fn, u8_t interval)
{ (void)pcb; (void)fn; (void)interval; }
void tcp_err(struct tcp_pcb *pcb, tcp_err_fn fn) { (void)pcb; (void)fn; }

static void client_request(http_client_t *client, struct tcp_pcb *pcb, const char *request)
{
    memset(client, 0, sizeof(*client));
    pcb->snd_buf = 32768;
    client->pcb = pcb;
    strcpy((char *)client->request, request);
    client->request_length = strlen(request);
    transmitted_length = 0;
}
static void ack_all(http_client_t *client)
{
    for (unsigned i = 0; i < 100 && client->pcb; ++i) {
        assert(client->static_inflight > 0);
        assert(http_sent(client, client->pcb, client->static_inflight) == ERR_OK);
    }
    assert(!client->pcb);
}

int main(int argc, char **argv)
{
    const unsigned mode = argc > 1 ? (unsigned)atoi(argv[1]) : 0;
    t384_frame_pipeline_init();
    struct tcp_pcb pcb = {0}, status_pcb = {0};
    http_client_t *client = &clients[0];
    client_request(client, &pcb, "POST /api/v1/module-files/read HTTP/1.1\r\nContent-Length: 9\r\nContent-Type: application/octet-stream\r\n\r\nnuct-high");
    assert(handle_module_request(client) == ERR_OK);
    assert(memcmp(transmitted, "HTTP/1.0 202", 12) == 0);
    ack_all(client);
    const t384_module_file_status_t *s = t384_module_files_status();
    for (now = 0; now < 10000 && s->state == T384_MF_READING; ++now)
        t384_module_files_task(now);
    assert(s->state == T384_MF_READY && active);
    char request[200];
    snprintf(request, sizeof(request), "GET /api/v1/module-files/data?transaction=%lu HTTP/1.1\r\n\r\n", (unsigned long)s->transaction);
    client_request(client, &pcb, request);
    backpressure = mode == 2;
    assert(handle_module_request(client) == ERR_OK);
    assert(active && client->module_download);
    if (mode == 1) {
        abort_client(client);
        assert(!active && s->state == T384_MF_ABORTED);
    } else if (mode == 2) {
        now += 15000;
        t384_http_status_task();
        assert(!active && !client->pcb && s->state == T384_MF_ABORTED);
    } else {
        /* Status polling uses client-local memory and cannot overwrite file data. */
        const uint8_t *leased = (const uint8_t *)client->static_data;
        uint8_t prefix[32]; memcpy(prefix, leased, sizeof(prefix));
        size_t before = transmitted_length;
        client_request(&clients[1], &status_pcb, "GET /api/v1/module-files/status HTTP/1.1\r\n\r\n");
        assert(handle_module_request(&clients[1]) == ERR_OK);
        ack_all(&clients[1]);
        assert(memcmp(prefix, leased, sizeof(prefix)) == 0);
        /* Restart only the test transcript after the status response. */
        memcpy(transmitted, leased, before); transmitted_length = before;
        ack_all(client);
        assert(!active && s->state == T384_MF_DONE && resumes == 1);
        const char *body = strstr((const char *)transmitted, "\r\n\r\n")+4;
        assert(transmitted_length-(size_t)(body-(const char *)transmitted) == 32768);
        for (unsigned i = 0; i < 32768; ++i) assert((uint8_t)body[i] == (uint8_t)i);
    }
    printf("module files HTTP ACK/abort/backpressure scenario %u passed\n", mode);
    return 0;
}
