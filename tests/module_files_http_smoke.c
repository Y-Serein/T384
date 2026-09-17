/* Exercise actual HTTP handlers and ACK ownership with a fake TCP transport. */
#define main transaction_fixture_main
#include "module_files_smoke.c"
#undef main
#include "../firmware/Common/App/http_status.c"

static uint8_t transmitted[40000];
static size_t transmitted_length;
static bool backpressure;
static err_t close_result;

uint32_t t384_millis(void) { return now; }
err_t tcp_write(struct tcp_pcb *pcb, const void *data, u16_t length, u8_t flags)
{
    assert(flags & TCP_WRITE_FLAG_COPY);
    if (backpressure) return ERR_MEM;
    assert(length <= pcb->snd_buf);
    pcb->snd_buf = (u16_t)(pcb->snd_buf - length);
    assert(transmitted_length + length <= sizeof(transmitted));
    memcpy(transmitted+transmitted_length, data, length);
    transmitted_length += length;
    return ERR_OK;
}
err_t tcp_output(struct tcp_pcb *pcb) { (void)pcb; return ERR_OK; }
/* Like lwIP, successful close may leave a PCB waiting for FIN/ACK. */
err_t tcp_close(struct tcp_pcb *pcb) { (void)pcb; return close_result; }
void tcp_abort(struct tcp_pcb *pcb) { (void)pcb; }
void tcp_arg(struct tcp_pcb *pcb, void *arg) { pcb->callback_arg = arg; }
void tcp_recv(struct tcp_pcb *pcb, tcp_recv_fn fn) { pcb->recv = fn; }
void tcp_sent(struct tcp_pcb *pcb, tcp_sent_fn fn) { pcb->sent = fn; }
void tcp_poll(struct tcp_pcb *pcb, tcp_poll_fn fn, u8_t interval)
{ pcb->poll = fn; pcb->pollinterval = interval; }
void tcp_err(struct tcp_pcb *pcb, tcp_err_fn fn) { pcb->errf = fn; }

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
        const u16_t acked = client->static_inflight;
        client->pcb->snd_buf = (u16_t)(client->pcb->snd_buf + acked);
        assert(http_sent(client, client->pcb, acked) == ERR_OK);
    }
    assert(!client->pcb);
}

#ifndef T384_HTTP_FIXTURE_NO_MAIN
static err_t lifecycle_receive(void *arg, struct tcp_pcb *pcb,
                               struct pbuf *p, err_t error)
{ (void)arg; (void)pcb; (void)p; return error; }

static void lifecycle_scenario(unsigned mode)
{
    struct tcp_pcb old = {0}, replacement = {0};
    http_client_t *client = &clients[0];
    client_request(client, &old, "GET /diag HTTP/1.1\r\n\r\n");
    tcp_arg(&old, client);
    tcp_recv(&old, lifecycle_receive);
    tcp_sent(&old, http_sent);
    tcp_poll(&old, http_poll, HTTP_POLL_INTERVAL);
    tcp_err(&old, http_error);
    if (mode == 4u) {
        close_result = ERR_MEM;
        assert(close_client(client) == ERR_OK);
        assert(client->closing && client->pcb == &old);
        assert(old.callback_arg == client && old.recv == lifecycle_receive);
        assert(old.sent == http_sent && old.poll == http_poll);
        assert(old.pollinterval == HTTP_POLL_INTERVAL && old.errf == http_error);
        close_result = ERR_OK;
        assert(old.poll(old.callback_arg, &old) == ERR_OK);
    } else {
        assert(close_client(client) == ERR_OK);
    }
    assert(!client->pcb);
    client_request(client, &replacement, "GET /raw16.stream HTTP/1.1\r\n\r\n");
    client->raw16_response = true;
    raw16_client = client;
    /* Deliver the old close's delayed events after reusing the HTTP slot. */
    assert(old.callback_arg == NULL && old.recv == NULL && old.sent == NULL);
    assert(old.poll == NULL && old.errf == NULL && old.pollinterval == 0u);
    if (old.errf) old.errf(old.callback_arg, ERR_RST);
    if (old.poll) old.poll(old.callback_arg, &old);
    assert(client->pcb == &replacement && raw16_client == client);
    abort_client(client);
    puts("HTTP closed-PCB callback isolation/retry scenario passed");
}

static void static_window_scenario(unsigned mode)
{
    static char body[20000];
    for (size_t i = 0; i < sizeof(body); ++i) body[i] = (char)(i & 127u);
    struct tcp_pcb pcb = {0};
    http_client_t *client = &clients[0];
    client_request(client, &pcb, "GET / HTTP/1.1\r\n\r\n");
    pcb.snd_buf = 2u * TCP_MSS;
    assert(send_static_response(client, body, sizeof(body)) == ERR_OK);
    assert(client->static_inflight == 2u * TCP_MSS && pcb.snd_buf == 0u);
    assert(client->static_offset == 2u * TCP_MSS);
    /* A partial ACK should immediately refill available space. */
    pcb.snd_buf = TCP_MSS;
    backpressure = mode == 6u;
    assert(http_sent(client, &pcb, TCP_MSS) == ERR_OK);
    if (backpressure) {
        assert(client->static_inflight == TCP_MSS);
        assert(client->static_offset == 2u * TCP_MSS);
        backpressure = false;
        assert(queue_static_chunk(client) == ERR_OK);
    }
    assert(client->static_inflight == 2u * TCP_MSS && pcb.snd_buf == 0u);
    assert(client->static_offset == 3u * TCP_MSS);
    while (client->static_offset < sizeof(body)) {
        pcb.snd_buf = TCP_MSS;
        assert(http_sent(client, &pcb, TCP_MSS) == ERR_OK);
    }
    assert(client->pcb == &pcb && client->static_inflight > 0u);
    assert(transmitted_length == sizeof(body));
    assert(memcmp(transmitted, body, sizeof(body)) == 0);
    ack_all(client);
    puts("HTTP static window/partial ACK/backpressure/final ACK ownership passed");
}

int main(int argc, char **argv)
{
    const unsigned mode = argc > 1 ? (unsigned)atoi(argv[1]) : 0;
    t384_frame_pipeline_init();
    if (mode == 3u || mode == 4u) { lifecycle_scenario(mode); return 0; }
    if (mode == 5u || mode == 6u) { static_window_scenario(mode); return 0; }
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
#endif
