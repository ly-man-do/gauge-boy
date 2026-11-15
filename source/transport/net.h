#ifndef GAUGEBOY_NET_H
#define GAUGEBOY_NET_H

/*
 * 3DS network service (libctru soc). Call net_init() once at startup,
 * before any tcp_transport use, and net_exit() at shutdown.
 */

#include <stdbool.h>
#include <stddef.h>

/* Returns false if the soc service could not start (e.g. Wi-Fi switched off). */
bool net_init(void);
void net_exit(void);

/* One line describing the console's network link for the log, e.g.
 * "Wi-Fi on, \"V-LINK\" (slot 1), 3DS IP 192.168.0.11" or
 * "Wi-Fi not connected (join the adapter's network in System Settings)".
 * Matches spike_net_info_fn. */
void net_describe(char *out, size_t len);

#endif
