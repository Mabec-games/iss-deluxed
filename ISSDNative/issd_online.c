#include "issd_online.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <SDL.h>

#include "issd_signal.h"
#include "snes_lobby_client.h"

#ifndef ISSD_ONLINE_VERSION
#define ISSD_ONLINE_VERSION "dev"
#endif

enum {
    INPUT_DELAY = 3,            /* frames; absorbs ordinary internet jitter */
    CONNECT_TIMEOUT_MS = 15000, /* to the matchmaking service */
    HELLO_INTERVAL_MS = 2000,   /* guest: one small message every two seconds */
    HELLO_ATTEMPTS = 15         /* ...for up to thirty seconds */
};

/* Messages between the launchers, sealed by issd_signal:
 *   'H' guest -> host  hello:   game version
 *   'W' host  -> guest welcome: session id (4 bytes) + game version
 *   'V' host  -> guest:         wrong version */

static IssdOnlineState s_state;
static bool s_hosting;
static char s_code[40], s_message[160];
static IssdOnlineLaunch s_launch;
static Uint32 s_since, s_next_hello;
static int s_hellos;

static void fail(const char *message) {
    snprintf(s_message, sizeof s_message, "%s", message);
    issd_signal_close();
    s_state = ISSD_ONLINE_FAILED;
}

/* A rough region from the computer's time zone, shown in front of the code
 * so the guest knows what lag to expect. Nothing is looked up online. */
static const char *region(void) {
    char zone[128] = "";
    const char *tz = getenv("TZ");
    if (tz && tz[0]) snprintf(zone, sizeof zone, "%s", tz[0] == ':' ? tz + 1 : tz);
#ifndef _WIN32
    if (!zone[0]) {
        char link[256];
        ssize_t n = readlink("/etc/localtime", link, sizeof link - 1);
        if (n > 0) {
            link[n] = '\0';
            const char *z = strstr(link, "zoneinfo/");
            snprintf(zone, sizeof zone, "%s", z ? z + 9 : link);
        }
    }
#endif
    static const struct { const char *prefix, *name; } map[] = {
        { "Europe/", "EU" }, { "Africa/", "AF" }, { "Asia/", "ASIA" }, { "Australia/", "OCE" },
        { "Pacific/", "OCE" }, { "America/Sao_Paulo", "SA" }, { "America/Argentina", "SA" },
        { "America/Santiago", "SA" }, { "America/Bogota", "SA" }, { "America/Lima", "SA" },
        { "America/Caracas", "SA" }, { "America/Montevideo", "SA" }, { "America/", "NA" },
        { "US/", "NA" }, { "Canada/", "NA" },
    };
    for (size_t i = 0; i < sizeof map / sizeof *map; i++)
        if (!strncmp(zone, map[i].prefix, strlen(map[i].prefix))) return map[i].name;
    return "WORLD";
}

static void make_code(char *out, size_t size) {
    static const char *words[] = {
        "TIGER", "EAGLE", "LION", "FALCON", "COMET", "ROCKET", "THUNDER", "STORM",
        "GOAL", "STRIKER", "KEEPER", "CORNER", "VOLLEY", "HEADER", "TACKLE", "DRIBBLE",
        "GOLD", "SILVER", "BLAZE", "FROST", "SHADOW", "VIPER", "COBRA", "PANTHER",
        "WOLF", "BEAR", "SHARK", "HAWK", "RAVEN", "PHOENIX", "DRAGON", "TITAN"
    };
    static bool seeded;
    if (!seeded) { srand((unsigned)time(NULL) ^ (unsigned)SDL_GetPerformanceCounter()); seeded = true; }
    int number = (rand() % 1000) * 1000 + rand() % 1000;   /* six digits */
    snprintf(out, size, "%s-%s-%06d", region(), words[rand() % (int)(sizeof words / sizeof *words)], number);
}

/* Codes are typed by people: accept any case and stray spaces. */
static void normalise(const char *in, char *out, size_t size) {
    size_t n = 0;
    for (; *in && n + 1 < size; in++)
        if (isalnum((unsigned char)*in) || *in == '-') out[n++] = (char)toupper((unsigned char)*in);
    out[n] = '\0';
}

/* Callers cancel any previous attempt and set s_code first. */
static bool begin(bool hosting) {
    s_hosting = hosting;
    s_hellos = 0;
    s_since = SDL_GetTicks();
    s_next_hello = 0;
    if (!issd_signal_open(s_code, hosting)) {
        fail(issd_signal_error()[0] ? issd_signal_error() : "Could not start online play.");
        return false;
    }
    s_state = ISSD_ONLINE_CONNECTING;
    snprintf(s_message, sizeof s_message, "Connecting...");
    return true;
}

void issd_online_host(void) {
    issd_online_cancel();
    make_code(s_code, sizeof s_code);
    begin(true);
}

void issd_online_join(const char *code) {
    issd_online_cancel();
    normalise(code, s_code, sizeof s_code);
    if (strlen(s_code) < 8) {
        snprintf(s_message, sizeof s_message, "Type the invite code from the host.");
        s_state = ISSD_ONLINE_FAILED;
        return;
    }
    begin(false);
}

void issd_online_cancel(void) {
    if (s_state != ISSD_ONLINE_IDLE && s_state != ISSD_ONLINE_LAUNCH) issd_signal_close();
    s_state = ISSD_ONLINE_IDLE;
    s_message[0] = '\0';
    s_code[0] = '\0';
}

static void launch(bool host, uint32_t session) {
    issd_signal_set_match(host, session);
    memset(&s_launch, 0, sizeof s_launch);
    s_launch.local_slot = host ? 0 : 1;
    s_launch.session_id = session;
    s_launch.input_delay = INPUT_DELAY;
    snprintf(s_launch.bind_hostport, sizeof s_launch.bind_hostport, "0.0.0.0:%d", host ? 7777 : 7778);
    snprintf(s_launch.peer_hostport, sizeof s_launch.peer_hostport, "0.0.0.0:%d", host ? 7778 : 7777);
    s_state = ISSD_ONLINE_LAUNCH;
    snprintf(s_message, sizeof s_message, "Connecting to your opponent...");
}

IssdOnlineState issd_online_poll(void) {
    if (s_state == ISSD_ONLINE_IDLE || s_state == ISSD_ONLINE_FAILED || s_state == ISSD_ONLINE_LAUNCH)
        return s_state;
    issd_signal_pump();
    if (issd_signal_error()[0]) { fail(issd_signal_error()); return s_state; }
    const Uint32 now = SDL_GetTicks();

    if (s_state == ISSD_ONLINE_CONNECTING) {
        if (issd_signal_ready()) {
            s_state = s_hosting ? ISSD_ONLINE_WAITING : ISSD_ONLINE_SEARCHING;
            snprintf(s_message, sizeof s_message, s_hosting ? "Waiting for your opponent to join..."
                                                            : "Looking for the host...");
        } else if (now - s_since > CONNECT_TIMEOUT_MS) {
            fail("The online matchmaking service did not answer. Try again later.");
        }
        return s_state;
    }

    char kind;
    uint8_t data[128];
    size_t len;
    while (issd_signal_recv(&kind, data, sizeof data - 1, &len)) {
        data[len] = '\0';
        if (s_hosting && kind == 'H') {
            if (strcmp((const char *)data, ISSD_ONLINE_VERSION) != 0) {
                issd_signal_send('V', ISSD_ONLINE_VERSION, strlen(ISSD_ONLINE_VERSION));
                snprintf(s_message, sizeof s_message, "Someone tried to join with a different version.");
                continue;
            }
            uint32_t session = 0;
            while (!session) session = (uint32_t)rand() << 16 ^ (uint32_t)rand() ^ (uint32_t)now;
            uint8_t welcome[4 + 64];
            welcome[0] = (uint8_t)(session >> 24); welcome[1] = (uint8_t)(session >> 16);
            welcome[2] = (uint8_t)(session >> 8);  welcome[3] = (uint8_t)session;
            size_t vlen = strlen(ISSD_ONLINE_VERSION);
            memcpy(welcome + 4, ISSD_ONLINE_VERSION, vlen);
            if (issd_signal_send('W', welcome, 4 + vlen)) {
                issd_signal_set_welcome(welcome, 4 + vlen);
                launch(true, session);
            }
            return s_state;
        }
        if (!s_hosting && kind == 'W' && len >= 4) {
            uint32_t session = (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
                               (uint32_t)data[2] << 8 | data[3];
            launch(false, session);
            return s_state;
        }
        if (!s_hosting && kind == 'V') {
            fail("The host is running a different version of ISS DELUXE'd.");
            return s_state;
        }
    }

    if (s_state == ISSD_ONLINE_SEARCHING && (Sint32)(now - s_next_hello) >= 0) {
        if (s_hellos >= HELLO_ATTEMPTS) {
            fail("No host answered with that invite code. Check the code with the host.");
            return s_state;
        }
        issd_signal_send('H', ISSD_ONLINE_VERSION, strlen(ISSD_ONLINE_VERSION));
        s_hellos++;
        s_next_hello = now + HELLO_INTERVAL_MS;
    }
    return s_state;
}

const char *issd_online_code(void) { return s_code; }
const char *issd_online_message(void) { return s_message; }

bool issd_online_take_launch(IssdOnlineLaunch *out) {
    if (s_state != ISSD_ONLINE_LAUNCH || !out) return false;
    *out = s_launch;
    return true;
}

void issd_online_match_connected(void) { issd_signal_close(); }
