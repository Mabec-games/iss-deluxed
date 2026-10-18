#ifndef ISSD_ONLINE_H
#define ISSD_ONLINE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Online HOST/JOIN with invite codes such as EU-TIGER-482913.
 *
 * The two games meet through public MQTT brokers (issd_signal.c), with the
 * invite code as the key; nobody types or sees an IP address. The match
 * itself runs peer to peer (ICE). Broker traffic is a handful of small
 * messages per match. */

typedef enum {
    ISSD_ONLINE_IDLE,
    ISSD_ONLINE_CONNECTING,
    ISSD_ONLINE_WAITING,      /* host: room open, waiting for the guest */
    ISSD_ONLINE_SEARCHING,    /* guest: looking for the room */
    ISSD_ONLINE_JOINED,       /* guest: seated, waiting for the host */
    ISSD_ONLINE_STARTING,
    ISSD_ONLINE_LAUNCH,       /* match ready: take it with issd_online_take_launch */
    ISSD_ONLINE_FAILED
} IssdOnlineState;

typedef struct {
    int local_slot;           /* 0 host = player 1, 1 guest = player 2 */
    uint32_t session_id;
    int input_delay;
    char bind_hostport[64];
    char peer_hostport[64];
} IssdOnlineLaunch;

void issd_online_host(void);
void issd_online_join(const char *code);
/* Leave the room and disconnect from the lobby. */
void issd_online_cancel(void);
/* Advance the handshake; call once per launcher frame. */
IssdOnlineState issd_online_poll(void);
const char *issd_online_code(void);
const char *issd_online_message(void);
bool issd_online_take_launch(IssdOnlineLaunch *out);

/* Called from the game loop once the match connection is up: the lobby is
 * no longer needed, so the server carries nothing during play. */
void issd_online_match_connected(void);

#ifdef __cplusplus
}
#endif

#endif
