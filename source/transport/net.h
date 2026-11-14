#ifndef GAUGEBOY_NET_H
#define GAUGEBOY_NET_H

/*
 * 3DS network service (libctru soc). Call net_init() once at startup,
 * before any tcp_transport use, and net_exit() at shutdown.
 */

#include <stdbool.h>

/* Returns false if the soc service could not start (e.g. Wi-Fi switched off). */
bool net_init(void);
void net_exit(void);

#endif
