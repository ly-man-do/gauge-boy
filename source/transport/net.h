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

/* If the console is not on Wi-Fi, asks the system to join one of the
 * saved connections, as Luma3DS's "Force wireless connection" does, and
 * waits up to NET_JOIN_TIMEOUT_MS. Call from the worker thread, never the
 * render loop. Matches spike_net_up_fn: msg gets a log line, or stays
 * empty if the console was already on Wi-Fi. */
#define NET_JOIN_TIMEOUT_MS 20000u
bool net_bring_up(char *msg, size_t len);

/* Which System Settings connection slot net_bring_up may join: 0 = any
 * saved one in range, 1-3 = only that slot. With a slot set, CONNECT
 * also moves the 3DS off another network it is on. */
void net_set_wifi_slot(int slot);

/* The network name saved in connection slot 1-3. Returns false (out gets
 * "(not set up)") if the slot is empty or can't be read. */
bool net_slot_name(int slot, char *out, size_t len);

#endif
