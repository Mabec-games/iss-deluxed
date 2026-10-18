#ifndef ISSD_SIGNAL_H
#define ISSD_SIGNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Serverless matchmaking for online play: the two games meet through free
 * public MQTT brokers (no account, no login). The invite code names a
 * private channel only by its hash and is the key that encrypts and
 * authenticates every message, so the brokers and anyone listening see
 * nothing readable. Only a few small messages are exchanged per match: a
 * hello, a welcome, and the ICE connection details; the match itself is
 * peer to peer. This module also provides the snes_lobby_* functions the
 * engine's netplay uses for ICE signalling. */

/* Connect to the first reachable broker and listen on this side's channel.
 * Returns false at once only for bad arguments; connection progress is
 * reported by issd_signal_ready() / issd_signal_error(). */
bool issd_signal_open(const char *code, bool host);
void issd_signal_close(void);
void issd_signal_pump(void);
bool issd_signal_ready(void);           /* connected and subscribed */
const char *issd_signal_error(void);    /* non-empty after a failure */

/* Control messages between the two launchers (not ICE signals). */
bool issd_signal_send(char kind, const void *data, size_t len);
bool issd_signal_recv(char *kind, uint8_t *data, size_t cap, size_t *len);

/* Host: the welcome to repeat if the guest's hello arrives again. */
void issd_signal_set_welcome(const void *data, size_t len);

/* Once both sides agree on a match: seat and session for the netplay core. */
void issd_signal_set_match(bool host, uint32_t session_id);

#ifdef __cplusplus
}
#endif

#endif
