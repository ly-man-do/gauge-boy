#ifndef GAUGEBOY_TEST_SERVER_H
#define GAUGEBOY_TEST_SERVER_H

/* Scripted TCP server on a thread, for transport and spike tests. */

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    S_ACCEPT,     /* accept the next connection */
    S_REPLY,      /* read a command up to CR, then send arg */
    S_CLOSE,      /* close the current connection */
    S_WAIT_CLOSE, /* keep the connection open until the client closes it */
    S_END
} step_kind_t;

typedef struct {
    step_kind_t kind;
    const char *arg;
} step_t;

typedef struct {
    int listen_fd;
    uint16_t port;
    const step_t *steps;
    pthread_t thread;
    char received[512];
    size_t received_len;
} server_t;

/* Listen on 127.0.0.1 (any free port) and run steps on a thread. */
void server_start(server_t *s, const step_t *steps);
/* Wait for the script to finish, then stop listening. */
void server_stop(server_t *s);
/* A local port with nothing listening on it. */
uint16_t closed_port(void);
void sleep_ms(unsigned ms);

#endif
