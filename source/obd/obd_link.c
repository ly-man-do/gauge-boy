#include "obd_link.h"

#include <stdio.h>
#include <string.h>

#include "obd_reply.h"

/* Keeps a reply's supported bits (OR over all ECUs). False if no ECU
 * sent "41 <range>" with four data bytes. */
static bool read_range(const elm_response_t *r, int protocol, uint8_t range_pid,
                       uint8_t bits[4], int *ecus)
{
    static obd_reply_t reply;
    if (obd_assemble(r->lines, r->line_count, protocol, false, &reply) != OBD_OK) {
        return false;
    }
    bool any = false;
    int answered = 0;
    memset(bits, 0, 4);
    for (int e = 0; e < reply.ecu_count; e++) {
        for (int m = 0; m < reply.ecu[e].msg_count; m++) {
            const obd_msg_t *msg = &reply.ecu[e].msg[m];
            if (msg->len >= 6 && msg->data[0] == 0x41 && msg->data[1] == range_pid) {
                for (int i = 0; i < 4; i++) {
                    bits[i] |= msg->data[2 + i];
                }
                any = true;
                answered++;
                break;
            }
        }
    }
    if (ecus != NULL) {
        *ecus = answered;
    }
    return any;
}

static obd_link_result_t fail(obd_link_t *out, obd_link_result_t result, elm_status_t st,
                              const char *cmd)
{
    out->result = result;
    out->elm_status = st;
    out->failed_command = cmd;
    return result;
}

static bool lost(elm_status_t st)
{
    return st == ELM_ERR_IO || st == ELM_ERR_TIMEOUT;
}

static obd_link_trace_fn trace_fn;
static void *trace_ctx;

/* elm_command plus the trace callback. */
static elm_status_t command(elm_t *elm, const char *cmd, unsigned timeout_ms, elm_response_t *r)
{
    uint64_t t0 = elm->io.now_ms != NULL ? elm->io.now_ms(elm->io.ctx) : 0;
    elm_status_t st = elm_command(elm, cmd, timeout_ms, r);
    if (trace_fn != NULL) {
        uint64_t t1 = elm->io.now_ms != NULL ? elm->io.now_ms(elm->io.ctx) : t0;
        trace_fn(trace_ctx, cmd, r, (unsigned)(t1 - t0));
    }
    return st;
}

obd_link_result_t obd_link_open(elm_t *elm, bool reset, obd_link_t *out)
{
    return obd_link_open_traced(elm, reset, out, NULL, NULL);
}

obd_link_result_t obd_link_open_traced(elm_t *elm, bool reset, obd_link_t *out,
                                       obd_link_trace_fn trace, void *ctx)
{
    static elm_response_t first;   /* the 0100 reply, decoded after ATDPN */
    static elm_response_t r;
    memset(out, 0, sizeof *out);
    trace_fn = trace;
    trace_ctx = ctx;

    if (reset) {
        static elm_startup_t su;
        elm_status_t st = elm_startup(elm, &su);
        if (st != ELM_OK) {
            /* No reply at all to the reset means no adapter; a dropped
             * connection is "lost". */
            return fail(out, st == ELM_ERR_IO ? OBD_LINK_LOST : OBD_LINK_NO_ADAPTER, st,
                        su.failed_command);
        }
        memcpy(out->adapter_id, su.id, sizeof out->adapter_id);
    }

    /* First request: the adapter searches the protocols. */
    elm_status_t st = command(elm, "0100", ELM_TIMEOUT_SEARCH_MS, &first);
    if (st != ELM_OK) {
        obd_link_result_t res = lost(st) ? OBD_LINK_LOST
                              : (st == ELM_ERR_UNKNOWN_COMMAND) ? OBD_LINK_NO_ADAPTER
                              : OBD_LINK_NO_VEHICLE;
        return fail(out, res, st, "0100");
    }

    st = command(elm, "ATDPN", ELM_TIMEOUT_DEFAULT_MS, &r);
    if (st == ELM_OK && (r.line_count < 1 ||
                         !elm_parse_protocol(r.lines[r.line_count - 1], &out->protocol,
                                             &out->protocol_auto))) {
        st = ELM_ERR_BAD_REPLY;
    }
    if (st != ELM_OK || out->protocol < 1 || out->protocol > 12) {
        out->protocol = 0;
        return fail(out, lost(st) ? OBD_LINK_LOST : OBD_LINK_NO_ADAPTER,
                    st != ELM_OK ? st : ELM_ERR_BAD_REPLY, "ATDPN");
    }

    if (!read_range(&first, out->protocol, 0x00, out->supported[0], &out->ecus)) {
        return fail(out, OBD_LINK_NO_VEHICLE, ELM_ERR_BAD_REPLY, "0100");
    }
    out->ranges_read = 1;

    /* Further ranges while the last PID of the range says "next exists". */
    for (int i = 1; i < OBD_PID_RANGES && (out->supported[i - 1][3] & 0x01); i++) {
        char cmd[8];
        uint8_t range_pid = (uint8_t)(i * 0x20);
        snprintf(cmd, sizeof cmd, "01%02X", range_pid);
        st = command(elm, cmd, ELM_TIMEOUT_DEFAULT_MS, &r);
        if (lost(st)) {
            static char failed[8];
            memcpy(failed, cmd, sizeof failed);
            return fail(out, OBD_LINK_LOST, st, failed);
        }
        if (st != ELM_OK || !read_range(&r, out->protocol, range_pid, out->supported[i], NULL)) {
            break; /* a car may claim a range it then doesn't answer: keep what we have */
        }
        out->ranges_read = i + 1;
    }
    out->result = OBD_LINK_OK;
    out->elm_status = ELM_OK;
    return OBD_LINK_OK;
}

bool obd_link_pid_supported(const obd_link_t *link, uint8_t pid)
{
    if (pid == 0 || pid > OBD_PID_RANGES * 0x20) {
        return false;
    }
    int range = (pid - 1) / 0x20;
    if (range >= link->ranges_read) {
        return false;
    }
    int bit = (pid - 1) % 0x20; /* 0 = PID range+1, MSB of byte A */
    return (link->supported[range][bit / 8] >> (7 - bit % 8)) & 1;
}

char *obd_link_pid_list(const obd_link_t *link, char *out, size_t out_len)
{
    size_t n = 0;
    if (out_len == 0) {
        return out;
    }
    out[0] = '\0';
    for (int pid = 1; pid <= OBD_PID_RANGES * 0x20; pid++) {
        if (!obd_link_pid_supported(link, (uint8_t)pid)) {
            continue;
        }
        int w = snprintf(out + n, out_len - n, "%s%02X", n ? " " : "", pid);
        if (w < 0 || (size_t)w >= out_len - n) {
            break;
        }
        n += (size_t)w;
    }
    return out;
}

const char *obd_link_result_str(obd_link_result_t result)
{
    switch (result) {
    case OBD_LINK_OK:          return "Connected to the car";
    case OBD_LINK_NO_ADAPTER:  return "Adapter not answering properly";
    case OBD_LINK_NO_VEHICLE:  return "Car not answering (is the engine running?)";
    case OBD_LINK_LOST:        return "Connection to the adapter lost";
    case OBD_LINK_RESULT_COUNT: break;
    }
    return "?";
}
