/* Scripted TCP server on a thread, for transport and spike tests. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "test_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static void *server_main(void *arg)
{
    server_t *s = arg;
    int conn = -1;

    for (const step_t *st = s->steps; st->kind != S_END; st++) {
        switch (st->kind) {
        case S_ACCEPT:
            conn = accept(s->listen_fd, NULL, NULL);
            break;
        case S_REPLY: {
            char c;
            while (recv(conn, &c, 1, 0) == 1) {
                if (s->received_len < sizeof s->received - 1) {
                    s->received[s->received_len++] = c;
                    s->received[s->received_len] = '\0';
                }
                if (c == '\r') {
                    break;
                }
            }
            (void)send(conn, st->arg, strlen(st->arg), 0);
            break;
        }
        case S_CLOSE:
            close(conn);
            conn = -1;
            break;
        case S_WAIT_CLOSE: {
            char buf[64];
            while (recv(conn, buf, sizeof buf, 0) > 0) {
            }
            break;
        }
        case S_END:
            break;
        }
    }
    if (conn >= 0) {
        close(conn);
    }
    return NULL;
}

void server_start(server_t *s, const step_t *steps)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    int one = 1;

    memset(s, 0, sizeof *s);
    s->steps = steps;
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* any free port */
    bind(s->listen_fd, (struct sockaddr *)&addr, sizeof addr);
    listen(s->listen_fd, 4);
    getsockname(s->listen_fd, (struct sockaddr *)&addr, &len);
    s->port = ntohs(addr.sin_port);
    pthread_create(&s->thread, NULL, server_main, s);
}

void server_stop(server_t *s)
{
    pthread_join(s->thread, NULL);
    close(s->listen_fd);
}

uint16_t closed_port(void)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (struct sockaddr *)&addr, sizeof addr);
    getsockname(fd, (struct sockaddr *)&addr, &len);
    close(fd);
    return ntohs(addr.sin_port);
}

void sleep_ms(unsigned ms)
{
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    nanosleep(&ts, NULL);
}
