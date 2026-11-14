#include "fake_io.h"

#include <string.h>

void fake_io_init(fake_io_t *f)
{
    memset(f, 0, sizeof *f);
}

static void schedule(fake_io_t *f, const char *data, size_t len, uint64_t start,
                     size_t chunk_size)
{
    for (size_t i = 0; i < len; i++) {
        if (f->pending_head + f->pending_count >= FAKE_MAX_PENDING) {
            return;
        }
        int slot = f->pending_head + f->pending_count;
        f->pending[slot].at = start + (chunk_size ? i / chunk_size : 0);
        f->pending[slot].c = data[i];
        f->pending_count++;
    }
}

void fake_io_reply_n(fake_io_t *f, const char *cmd, const char *data, size_t len,
                     unsigned delay_ms, size_t chunk_size)
{
    if (f->reply_count >= FAKE_MAX_REPLIES) {
        return;
    }
    fake_reply_t *r = &f->replies[f->reply_count++];
    r->cmd = cmd;
    r->data = data;
    r->len = len;
    r->delay_ms = delay_ms;
    r->chunk_size = chunk_size;
}

void fake_io_inject_n(fake_io_t *f, const char *data, size_t len, uint64_t at_ms)
{
    schedule(f, data, len, at_ms, 0);
}

static int fake_write(void *ctx, const char *data, size_t len)
{
    fake_io_t *f = ctx;
    if (f->fail_writes) {
        return -1;
    }
    if (f->close_after_write) {
        f->closed = true;
    }
    if (f->written_len + len < FAKE_WRITTEN_MAX) {
        memcpy(f->written + f->written_len, data, len);
        f->written_len += len;
        f->written[f->written_len] = '\0';
    }

    /* The driver writes one command per call, ending in CR. */
    size_t cmd_len = len;
    if (cmd_len > 0 && data[cmd_len - 1] == '\r') {
        cmd_len--;
    }
    if (f->next_reply < f->reply_count) {
        fake_reply_t *r = &f->replies[f->next_reply];
        if (strlen(r->cmd) == cmd_len && memcmp(r->cmd, data, cmd_len) == 0) {
            f->next_reply++;
            schedule(f, r->data, r->len, f->now + r->delay_ms, r->chunk_size);
            return 0;
        }
    }
    f->unexpected_writes++;
    return 0;
}

static int fake_read(void *ctx, char *buf, size_t len, unsigned timeout_ms)
{
    fake_io_t *f = ctx;

    if (f->pending_count == 0) {
        if (f->closed) {
            return -1;
        }
        f->now += timeout_ms;
        return 0;
    }

    uint64_t first = f->pending[f->pending_head].at;
    if (first > f->now + timeout_ms) {
        f->now += timeout_ms;
        return 0;
    }
    if (first > f->now) {
        f->now = first;
    }

    size_t n = 0;
    while (n < len && f->pending_count > 0 &&
           f->pending[f->pending_head].at <= f->now) {
        buf[n++] = f->pending[f->pending_head].c;
        f->pending_head++;
        f->pending_count--;
    }
    return (int)n;
}

static uint64_t fake_now(void *ctx)
{
    return ((fake_io_t *)ctx)->now;
}

elm_io_t fake_io_iface(fake_io_t *f)
{
    elm_io_t io = { f, fake_write, fake_read, fake_now };
    return io;
}
