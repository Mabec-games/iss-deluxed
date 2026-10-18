#include "issd_signal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#include "sha256.h"
#include "snes_lobby_client.h"

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* Public brokers, tried in this order by both players so they meet on the
 * same one. All are free test services without accounts. */
static const char *const k_brokers[] = { "broker.hivemq.com", "broker.emqx.io", "test.mosquitto.org" };
enum {
    BROKER_COUNT = 3,
    CONNECT_MS = 4000,          /* per broker */
    KEEPALIVE_S = 60,
    PING_MS = 20000,
    KEY_ROUNDS = 20000,         /* slows down guessing a code from its channel name */
    MAX_PAYLOAD = 2304,
    QUEUE = 64
};

typedef struct { char kind; uint16_t len; uint8_t data[MAX_PAYLOAD]; } Message;

static struct {
    bool open, host, ready, matched;
    int fd, broker;
    Uint32 started, last_ping;
    uint8_t key[32], mac_key[32];
    char inbox[96], outbox[96];
    uint8_t rx[8192];
    size_t rx_len;
    char error[160];
    Message control[QUEUE], signals[QUEUE];
    int control_head, control_count, signal_head, signal_count;
    SnesLobbyJoinInfo join;
    uint8_t welcome[64];        /* host: repeated if the guest says hello again */
    size_t welcome_len;
} S = { .fd = -1 };

/* ---- crypto: SHA-256 in counter mode with a SHA-256 tag, keyed by the code */

static void hash2(const void *a, size_t alen, const void *b, size_t blen, uint8_t out[32]) {
    uint8_t buf[512];
    if (alen + blen > sizeof buf) { memset(out, 0, 32); return; }
    memcpy(buf, a, alen);
    memcpy(buf + alen, b, blen);
    sha256_compute(buf, alen + blen, out);
}

static void random_bytes(uint8_t *out, size_t n) {
    size_t got = 0;
#ifndef _WIN32
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { got = fread(out, 1, n, f); fclose(f); }
#endif
    for (; got < n; got++) out[got] = (uint8_t)(rand() ^ (int)SDL_GetPerformanceCounter());
}

static void keystream_xor(const uint8_t nonce[12], uint8_t *data, size_t len) {
    uint8_t block[48], ks[32];
    memcpy(block, S.key, 32);
    memcpy(block + 32, nonce, 12);
    for (size_t off = 0, n = 0; off < len; off += 32, n++) {
        block[44] = (uint8_t)(n >> 24); block[45] = (uint8_t)(n >> 16);
        block[46] = (uint8_t)(n >> 8);  block[47] = (uint8_t)n;
        sha256_compute(block, sizeof block, ks);
        for (size_t i = 0; i < 32 && off + i < len; i++) data[off + i] ^= ks[i];
    }
}

static void tag(const uint8_t *nonce_and_ct, size_t len, uint8_t out[32]) {
    uint8_t buf[32 + 12 + MAX_PAYLOAD + 1];
    memcpy(buf, S.mac_key, 32);
    memcpy(buf + 32, nonce_and_ct, len);
    sha256_compute(buf, 32 + len, out);
}

/* ---- MQTT 3.1.1, QoS 0, over plain TCP ---------------------------------- */

static void close_socket(void) {
#ifndef _WIN32
    if (S.fd >= 0) close(S.fd);
#endif
    S.fd = -1;
    S.ready = false;
    S.rx_len = 0;
}

static bool send_all(const uint8_t *p, size_t n) {
#ifndef _WIN32
    while (n) {
        ssize_t w = send(S.fd, p, n, MSG_NOSIGNAL);
        if (w < 0 && (errno == EAGAIN || errno == EINTR)) { SDL_Delay(1); continue; }
        if (w <= 0) return false;
        p += w; n -= (size_t)w;
    }
    return true;
#else
    (void)p; (void)n;
    return false;
#endif
}

static size_t put_length(uint8_t *out, size_t len) {
    size_t n = 0;
    do { uint8_t b = len % 128; len /= 128; out[n++] = (uint8_t)(b | (len ? 0x80 : 0)); } while (len);
    return n;
}

static bool mqtt_packet(uint8_t type, const uint8_t *body, size_t len) {
    uint8_t head[5];
    head[0] = type;
    size_t n = 1 + put_length(head + 1, len);
    return send_all(head, n) && (!len || send_all(body, len));
}

static size_t put_string(uint8_t *out, const char *s) {
    size_t n = strlen(s);
    out[0] = (uint8_t)(n >> 8); out[1] = (uint8_t)n;
    memcpy(out + 2, s, n);
    return n + 2;
}

static bool start_broker(void) {
#ifndef _WIN32
    while (S.broker < BROKER_COUNT) {
        struct addrinfo hints = {0}, *res = NULL;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(k_brokers[S.broker], "1883", &hints, &res) == 0 && res) {
            S.fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            if (S.fd >= 0) {
                fcntl(S.fd, F_SETFL, fcntl(S.fd, F_GETFL, 0) | O_NONBLOCK);
                int rc = connect(S.fd, res->ai_addr, res->ai_addrlen);
                freeaddrinfo(res);
                if (rc == 0 || errno == EINPROGRESS) { S.started = SDL_GetTicks(); return true; }
                close_socket();
            } else {
                freeaddrinfo(res);
            }
        }
        S.broker++;
    }
#endif
    snprintf(S.error, sizeof S.error, "Could not reach the online matchmaking service. Check your internet connection.");
    return false;
}

static bool send_connect(void) {
    uint8_t body[64], id[6];
    char client[24];
    random_bytes(id, sizeof id);
    snprintf(client, sizeof client, "issdx%02x%02x%02x%02x%02x%02x", id[0], id[1], id[2], id[3], id[4], id[5]);
    size_t n = put_string(body, "MQTT");
    body[n++] = 4;                      /* protocol level 3.1.1 */
    body[n++] = 0x02;                   /* clean session */
    body[n++] = 0; body[n++] = KEEPALIVE_S;
    n += put_string(body + n, client);
    return mqtt_packet(0x10, body, n);
}

bool issd_signal_send(char kind, const void *data, size_t len);

static void queue_message(char kind, const uint8_t *data, size_t len) {
    /* A hello after the match was arranged means the welcome got lost. */
    if (S.matched && S.host && kind == 'H' && S.welcome_len) {
        issd_signal_send('W', S.welcome, S.welcome_len);
        return;
    }
    bool signal = kind == 'S';
    Message *ring = signal ? S.signals : S.control;
    int *head = signal ? &S.signal_head : &S.control_head, *count = signal ? &S.signal_count : &S.control_count;
    if (*count == QUEUE || len > MAX_PAYLOAD) return;
    Message *m = &ring[(*head + *count) % QUEUE];
    m->kind = kind;
    m->len = (uint16_t)len;
    memcpy(m->data, data, len);
    (*count)++;
}

/* A sealed message: nonce(12) | ciphertext(kind + data) | tag(16). */
static void on_publish(const uint8_t *payload, size_t len) {
    if (len < 12 + 1 + 16 || len > 12 + 1 + MAX_PAYLOAD + 16) return;
    uint8_t expect[32], plain[1 + MAX_PAYLOAD];
    tag(payload, len - 16, expect);
    if (memcmp(expect, payload + len - 16, 16) != 0) return;   /* not ours, or tampered */
    size_t n = len - 12 - 16;
    memcpy(plain, payload + 12, n);
    keystream_xor(payload, plain, n);
    queue_message((char)plain[0], plain + 1, n - 1);
}

static void handle_packet(uint8_t type, const uint8_t *p, size_t len) {
    switch (type >> 4) {
    case 2:                                         /* CONNACK */
        if (len >= 2 && p[1] == 0) {
            uint8_t body[128];
            size_t n = 0;
            body[n++] = 0; body[n++] = 1;           /* packet id */
            n += put_string(body + n, S.inbox);
            body[n++] = 0;                          /* QoS 0 */
            mqtt_packet(0x82, body, n);
        }
        break;
    case 9:                                         /* SUBACK */
        S.ready = len >= 3 && p[2] != 0x80;
        S.last_ping = SDL_GetTicks();
        break;
    case 3:                                         /* PUBLISH */
        if (len >= 2) {
            size_t topic = (size_t)p[0] << 8 | p[1];
            if (2 + topic <= len) on_publish(p + 2 + topic, len - 2 - topic);
        }
        break;
    default:
        break;
    }
}

static void read_socket(void) {
#ifndef _WIN32
    for (;;) {
        if (S.rx_len == sizeof S.rx) { S.rx_len = 0; return; }   /* oversized: drop */
        ssize_t r = recv(S.fd, S.rx + S.rx_len, sizeof S.rx - S.rx_len, 0);
        if (r > 0) { S.rx_len += (size_t)r; continue; }
        if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            close_socket();
            snprintf(S.error, sizeof S.error, "Lost the connection to the matchmaking service.");
        }
        break;
    }
    while (S.rx_len >= 2) {
        size_t len = 0, mult = 1, i = 1;
        for (; i < S.rx_len && i < 5; i++) {
            len += (S.rx[i] & 127) * mult;
            mult *= 128;
            if (!(S.rx[i] & 128)) break;
        }
        if (i >= S.rx_len || (S.rx[i] & 128)) return;   /* length not complete yet */
        size_t head = i + 1;
        if (S.rx_len < head + len) return;
        handle_packet(S.rx[0], S.rx + head, len);
        memmove(S.rx, S.rx + head + len, S.rx_len - head - len);
        S.rx_len -= head + len;
    }
#endif
}

/* ---- public API ------------------------------------------------------------ */

bool issd_signal_open(const char *code, bool host) {
    issd_signal_close();
    if (!code || !code[0]) return false;
    /* Key: the code stretched, so a channel name cannot be traced back to
     * its code by trying every possibility cheaply. */
    uint8_t k[32];
    hash2("issdx-key:", 10, code, strlen(code), k);
    for (int i = 0; i < KEY_ROUNDS; i++) hash2(k, 32, code, strlen(code), k);
    memcpy(S.key, k, 32);
    hash2("issdx-mac:", 10, k, 32, S.mac_key);
    uint8_t channel[32];
    hash2("issdx-topic:", 12, k, 32, channel);
    char hex[25];
    for (int i = 0; i < 12; i++) snprintf(hex + i * 2, 3, "%02x", channel[i]);
    snprintf(S.inbox, sizeof S.inbox, "issdx/v1/%s/%s", hex, host ? "h" : "g");
    snprintf(S.outbox, sizeof S.outbox, "issdx/v1/%s/%s", hex, host ? "g" : "h");
    S.host = host;
    S.open = true;
    S.broker = 0;
    S.error[0] = '\0';
    memset(&S.join, 0, sizeof S.join);
    return start_broker();
}

void issd_signal_close(void) {
    if (S.fd >= 0 && S.ready) mqtt_packet(0xE0, NULL, 0);     /* DISCONNECT */
    close_socket();
    S.open = S.matched = false;
    S.control_count = S.signal_count = 0;
}

void issd_signal_pump(void) {
#ifndef _WIN32
    if (!S.open || S.fd < 0) return;
    Uint32 now = SDL_GetTicks();
    if (!S.ready && S.started) {
        struct pollfd pfd = { S.fd, POLLOUT, 0 };
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLOUT)) {
            int err = 0;
            socklen_t l = sizeof err;
            getsockopt(S.fd, SOL_SOCKET, SO_ERROR, &err, &l);
            if (err == 0) { S.started = 0; send_connect(); }
        } else if (now - S.started > CONNECT_MS) {
            close_socket();
        }
        if (S.fd < 0) {                 /* this broker failed: try the next */
            S.broker++;
            if (!start_broker()) S.open = false;
            return;
        }
    }
    read_socket();
    if (S.ready && now - S.last_ping > PING_MS) {
        mqtt_packet(0xC0, NULL, 0);
        S.last_ping = now;
    }
#endif
}

bool issd_signal_ready(void) { return S.open && S.ready; }
const char *issd_signal_error(void) { return S.error; }

bool issd_signal_send(char kind, const void *data, size_t len) {
    if (!S.ready || len > MAX_PAYLOAD) return false;
    uint8_t sealed[12 + 1 + MAX_PAYLOAD + 32];
    random_bytes(sealed, 12);
    sealed[12] = (uint8_t)kind;
    if (len) memcpy(sealed + 13, data, len);
    keystream_xor(sealed, sealed + 12, 1 + len);
    uint8_t t[32];
    tag(sealed, 12 + 1 + len, t);
    memcpy(sealed + 13 + len, t, 16);
    size_t total = 13 + len + 16;
    uint8_t body[128 + sizeof sealed];
    size_t n = put_string(body, S.outbox);
    memcpy(body + n, sealed, total);
    return mqtt_packet(0x30, body, n + total);
}

bool issd_signal_recv(char *kind, uint8_t *data, size_t cap, size_t *len) {
    if (!S.control_count) return false;
    Message *m = &S.control[S.control_head];
    S.control_head = (S.control_head + 1) % QUEUE;
    S.control_count--;
    if (m->len > cap) return false;
    *kind = m->kind;
    memcpy(data, m->data, m->len);
    *len = m->len;
    return true;
}

void issd_signal_set_welcome(const void *data, size_t len) {
    S.welcome_len = len <= sizeof S.welcome ? len : 0;
    if (S.welcome_len) memcpy(S.welcome, data, len);
}

void issd_signal_set_match(bool host, uint32_t session_id) {
    S.matched = true;
    S.host = host;
    memset(&S.join, 0, sizeof S.join);
    S.join.ok = 1;
    S.join.session_id = session_id;
    S.join.local_slot = host ? 0 : 1;
    S.join.player_count = S.join.max_slots = 2;
}

/* ---- the lobby functions the engine's netplay uses for ICE signalling ---- */

const char *snes_lobby_default_url(void) { return "mqtt"; }
const char *snes_lobby_url(void) { return ""; }
int snes_lobby_connected(void) { return issd_signal_ready(); }
int snes_lobby_in_lobby(void) { return S.matched; }
int snes_lobby_is_host(void) { return S.host; }
const SnesLobbyJoinInfo *snes_lobby_join_info(void) { return &S.join; }
void snes_lobby_pump(void) { issd_signal_pump(); }
int snes_lobby_request_turn_credentials(void) { return -1; }

const SnesLobbyTurnCredentials *snes_lobby_turn_credentials(void) {
    static SnesLobbyTurnCredentials none;   /* no relay: direct connections only */
    return &none;
}

int snes_lobby_send_signal(int type, int flag, const char *text) {
    uint8_t buf[2 + 2048];
    size_t n = text ? strlen(text) : 0;
    if (n > 2047) n = 2047;
    buf[0] = (uint8_t)type;
    buf[1] = (uint8_t)flag;
    if (n) memcpy(buf + 2, text, n);
    return issd_signal_send('S', buf, 2 + n) ? 0 : -1;
}

int snes_lobby_poll_signal(int *type, int *flag, char *text, size_t text_cap) {
    issd_signal_pump();
    if (!S.signal_count) return 0;
    Message *m = &S.signals[S.signal_head];
    S.signal_head = (S.signal_head + 1) % QUEUE;
    S.signal_count--;
    if (m->len < 2 || !text_cap) return 0;
    *type = m->data[0];
    *flag = m->data[1];
    size_t n = m->len - 2 < text_cap - 1 ? m->len - 2 : text_cap - 1;
    memcpy(text, m->data + 2, n);
    text[n] = '\0';
    return 1;
}
