#ifndef GAUGEBOY_FAKE_IO_H
#define GAUGEBOY_FAKE_IO_H

/*
 * Scripted fake adapter for host tests, with a simulated clock.
 *
 * fake_io_reply() queues the reply to the next expected command. When the
 * driver writes that command, the reply bytes are scheduled delay_ms later,
 * delivered chunk_size bytes per read, 1 ms apart (chunk_size 0 = all at
 * once). Reads advance the fake clock instead of sleeping.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elm327/elm327.h"

#define FAKE_MAX_REPLIES 32
#define FAKE_MAX_PENDING 4096
#define FAKE_WRITTEN_MAX 1024

typedef struct {
    const char *cmd;      /* expected command, without CR */
    const char *data;     /* reply bytes (may contain NULs) */
    size_t len;
    unsigned delay_ms;
    size_t chunk_size;
} fake_reply_t;

typedef struct {
    uint64_t now;

    fake_reply_t replies[FAKE_MAX_REPLIES];
    int reply_count;
    int next_reply;

    struct { uint64_t at; char c; } pending[FAKE_MAX_PENDING];
    int pending_head;
    int pending_count;

    char written[FAKE_WRITTEN_MAX];
    size_t written_len;
    int unexpected_writes;   /* commands with no matching scripted reply */

    bool closed;             /* reads return -1 once nothing is pending */
    bool close_after_write;  /* set closed on the next write (peer hangs up mid-reply) */
    bool fail_writes;
} fake_io_t;

void fake_io_init(fake_io_t *f);
elm_io_t fake_io_iface(fake_io_t *f);

void fake_io_reply_n(fake_io_t *f, const char *cmd, const char *data, size_t len,
                     unsigned delay_ms, size_t chunk_size);
/* Bytes that arrive at absolute time at_ms whatever is written. */
void fake_io_inject_n(fake_io_t *f, const char *data, size_t len, uint64_t at_ms);

/* For string literals; sizeof keeps embedded NUL bytes. */
#define fake_io_reply(f, cmd, lit, delay_ms, chunk) \
    fake_io_reply_n((f), (cmd), (lit), sizeof(lit) - 1, (delay_ms), (chunk))
#define fake_io_inject(f, lit, at_ms) \
    fake_io_inject_n((f), (lit), sizeof(lit) - 1, (at_ms))

#endif
