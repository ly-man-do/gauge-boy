#ifndef GAUGEBOY_CONNTEST_SERVER_H
#define GAUGEBOY_CONNTEST_SERVER_H

/*
 * 3DS only: answers the system's internet connection test from GaugeBoy
 * itself (DNS on UDP 53, test page on TCP 80), so the 3DS keeps a Wi-Fi
 * OBD-II adapter's network. Runs on its own thread; while the network is
 * down it keeps retrying, so it starts listening as soon as the 3DS
 * allows sockets (which may be in the middle of the system's own test).
 *
 * For this to be used, the adapter's connection slot needs a manual IP
 * (e.g. 192.168.0.11) with Primary DNS set to that same address.
 */

#include <stdbool.h>

typedef void (*ct_server_log_fn)(void *ctx, const char *line);

/* Call after net_init(). log may be called from the server thread. */
bool ct_server_start(ct_server_log_fn log, void *ctx);
void ct_server_stop(void);

#endif
