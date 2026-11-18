#include "obd_freeze.h"

#include <stdio.h>
#include <string.h>

#include "obd_codes.h"
#include "obd_pid.h"

#define FREEZE_TIMEOUT_MS 2000u

static elm_status_t command(elm_t *elm, const char *cmd, elm_response_t *r,
                            obd_link_trace_fn trace, void *ctx)
{
    uint64_t t0 = elm->io.now_ms != NULL ? elm->io.now_ms(elm->io.ctx) : 0;
    elm_status_t st = elm_command(elm, cmd, FREEZE_TIMEOUT_MS, r);
    if (trace != NULL) {
        uint64_t t1 = elm->io.now_ms != NULL ? elm->io.now_ms(elm->io.ctx) : t0;
        trace(ctx, cmd, r, (unsigned)(t1 - t0));
    }
    return st;
}

static bool lost(elm_status_t st)
{
    return st == ELM_ERR_IO || st == ELM_ERR_TIMEOUT;
}

bool obd_freeze_find(const obd_reply_t *reply, uint8_t pid, uint8_t frame, const uint8_t **data,
                     size_t *len)
{
    for (int e = 0; e < reply->ecu_count; e++) {
        for (int m = 0; m < reply->ecu[e].msg_count; m++) {
            const obd_msg_t *msg = &reply->ecu[e].msg[m];
            if (msg->len >= 3 && msg->data[0] == 0x42 && msg->data[1] == pid &&
                msg->data[2] == frame) {
                *data = msg->data + 3;
                *len = msg->len - 3u;
                return true;
            }
        }
    }
    return false;
}

/* "02 pid 00" assembled; false if it wasn't answered. */
static elm_status_t ask(elm_t *elm, int protocol, uint8_t pid, const uint8_t **data, size_t *len,
                        bool *found, obd_link_trace_fn trace, void *ctx)
{
    static elm_response_t r;
    static obd_reply_t reply;
    char cmd[8];
    snprintf(cmd, sizeof cmd, "02%02X00", (unsigned)pid);
    elm_status_t st = command(elm, cmd, &r, trace, ctx);
    *found = st == ELM_OK && obd_assemble(&r, protocol, false, &reply) == OBD_OK &&
             obd_freeze_find(&reply, pid, 0, data, len);
    return st;
}

elm_status_t obd_freeze_read(elm_t *elm, int protocol, obd_freeze_t *out,
                             obd_link_trace_fn trace, void *ctx)
{
    memset(out, 0, sizeof *out);
    const uint8_t *data;
    size_t len;
    bool found;

    /* The code that set frame 0. */
    elm_status_t st = ask(elm, protocol, 0x02, &data, &len, &found, trace, ctx);
    if (lost(st)) {
        return st;
    }
    if (!found || len < 2 || (data[0] == 0 && data[1] == 0)) {
        return ELM_OK;   /* no freeze frame */
    }
    obd_code_from_bytes(data[0], data[1], out->code);
    out->have = true;

    /* Which PIDs it holds: 4-byte bitmaps for 01-20, 21-40 ... */
    uint8_t supported[8][4];
    int ranges = 0;
    for (int r = 0; r < 8; r++) {
        st = ask(elm, protocol, (uint8_t)(r * 0x20), &data, &len, &found, trace, ctx);
        if (lost(st)) {
            return st;
        }
        if (!found || len < 4) {
            break;
        }
        memcpy(supported[r], data, 4);
        ranges = r + 1;
        if ((data[3] & 0x01) == 0) {
            break;   /* the next range isn't supported */
        }
    }

    /* Each supported PID GaugeBoy decodes. */
    for (int r = 0; r < ranges && out->count < OBD_FREEZE_MAX; r++) {
        for (int bit = 0; bit < 31 && out->count < OBD_FREEZE_MAX; bit++) {
            if ((supported[r][bit / 8] & (0x80 >> (bit % 8))) == 0) {
                continue;
            }
            uint8_t pid = (uint8_t)(r * 0x20 + 1 + bit);
            if (obd_pid_info(pid) == NULL) {
                continue;
            }
            st = ask(elm, protocol, pid, &data, &len, &found, trace, ctx);
            if (lost(st)) {
                return st;
            }
            float v;
            if (found && obd_pid_decode(pid, data, len, &v) == OBD_PID_OK) {
                out->pid[out->count] = pid;
                out->value[out->count] = v;
                out->count++;
            }
        }
    }
    return ELM_OK;
}

char *obd_freeze_text(const obd_freeze_t *f, int units, char *out, size_t len)
{
    out[0] = '\0';
    for (int i = 0; i < f->count; i++) {
        char value[32];
        obd_pid_format(f->pid[i], f->value[i], (obd_units_t)units, value, sizeof value);
        size_t used = strlen(out);
        snprintf(out + used, len - used, "%s%s %s", i ? ", " : "", obd_pid_info(f->pid[i])->label,
                 value);
    }
    return out;
}
