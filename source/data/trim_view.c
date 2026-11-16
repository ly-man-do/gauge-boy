#include "trim_view.h"

#include <string.h>

#include "obd/obd_pid.h"

#define NORMAL 10.0f    /* trims within +-10 % are normal */
#define QUIET  5.0f     /* "the other bank is fine" */

trim_zone_t trim_zone(float percent)
{
    float a = percent < 0.0f ? -percent : percent;
    return a <= 10.0f ? TRIM_ZONE_GREEN : a <= 20.0f ? TRIM_ZONE_AMBER : TRIM_ZONE_RED;
}

void trim_bar(float percent, int width, char *out)
{
    int mid = width / 2;
    float per_cell = TRIM_BAR_RANGE / (float)mid;
    for (int i = 0; i < width; i++) {
        out[i] = '.';
    }
    out[mid] = '|';
    /* Cells filled: rounded to the nearest, at least one for a visible trim. */
    float cells_f = percent / per_cell;
    int cells = (int)(cells_f < 0.0f ? cells_f - 0.5f : cells_f + 0.5f);
    bool over = cells > mid || cells < -mid;
    if (cells > mid) {
        cells = mid;
    } else if (cells < -mid) {
        cells = -mid;
    }
    for (int k = 1; k <= (cells < 0 ? -cells : cells); k++) {
        out[mid + (cells < 0 ? -k : k)] = '#';
    }
    if (over) {
        out[cells < 0 ? 0 : width - 1] = cells < 0 ? '<' : '>';
    }
    out[width] = '\0';
}

void trim_state_from_live(const obd_live_t *lv, trim_state_t *out)
{
    memset(out, 0, sizeof *out);
    static const uint8_t st[2] = { OBD_PID_STFT_B1, OBD_PID_STFT_B2 };
    static const uint8_t lt[2] = { OBD_PID_LTFT_B1, OBD_PID_LTFT_B2 };
    for (int b = 0; b < 2; b++) {
        trim_bank_t *k = &out->bank[b];
        bool polled = obd_live_get(lv, st[b]) != NULL || obd_live_get(lv, lt[b]) != NULL;
        k->have_short = obd_live_value(lv, st[b], &k->short_term);
        k->have_long = obd_live_value(lv, lt[b], &k->long_term);
        if (polled) {
            out->banks = b + 1;
        }
    }
    out->have_rpm = obd_live_value(lv, 0x0C, &out->rpm);
    out->have_speed = obd_live_value(lv, 0x0D, &out->speed_kmh);
}

bool trim_total(const trim_bank_t *b, float *total)
{
    if (!b->have_short && !b->have_long) {
        return false;
    }
    *total = (b->have_short ? b->short_term : 0.0f) + (b->have_long ? b->long_term : 0.0f);
    return true;
}

bool trim_at_idle(const trim_state_t *s)
{
    return s->have_rpm && s->rpm >= 400.0f && s->rpm <= 1100.0f &&
           (!s->have_speed || s->speed_kmh < 1.0f);
}

int trim_hints(const trim_state_t *s, const char **out, int max)
{
    int n = 0;
#define HINT(text) do { if (n < max) out[n++] = (text); } while (0)
    if (s->banks == 0) {
        HINT("This car doesn't report fuel trims.");
        return n;
    }
    if (s->have_rpm && s->rpm < 400.0f) {
        HINT("Start the engine: trims only mean something with it running.");
        return n;
    }
    float t[2] = { 0.0f, 0.0f };
    bool have[2] = { trim_total(&s->bank[0], &t[0]), s->banks > 1 && trim_total(&s->bank[1], &t[1]) };
    if (!have[0] && !have[1]) {
        HINT("Waiting for the first readings...");
        return n;
    }
    bool idle = trim_at_idle(s);
    if (s->banks == 1) {
        if (t[0] > NORMAL) {
            HINT(idle ? "Lean at idle: possible vacuum (intake) leak."
                      : "Lean: possible fuel delivery (pump, filter) or air flow sensor problem.");
        } else if (t[0] < -NORMAL) {
            HINT("Rich: possible leaking injector, high fuel pressure or O2 sensor.");
        }
    } else if (have[0] && have[1]) {
        if (t[0] > NORMAL && t[1] > NORMAL) {
            HINT(idle ? "Both banks lean at idle: possible vacuum leak."
                      : "Both banks lean: possible fuel delivery (pump, filter) or air flow sensor.");
        } else if (t[0] < -NORMAL && t[1] < -NORMAL) {
            HINT("Both banks rich: possible high fuel pressure, leaking injector or O2 sensor.");
        } else {
            for (int b = 0; b < 2; b++) {
                int o = 1 - b;
                bool other_ok = t[o] > -QUIET && t[o] < QUIET;
                if (t[b] > NORMAL && other_ok) {
                    HINT(b == 0 ? "Bank 1 only lean: check that bank's O2 sensor, injectors or an intake leak on that side."
                                : "Bank 2 only lean: check that bank's O2 sensor, injectors or an intake leak on that side.");
                } else if (t[b] < -NORMAL && other_ok) {
                    HINT(b == 0 ? "Bank 1 only rich: check that bank's injectors or O2 sensor."
                                : "Bank 2 only rich: check that bank's injectors or O2 sensor.");
                }
            }
        }
    }
    bool all_normal = true;
    for (int b = 0; b < s->banks; b++) {
        all_normal = all_normal && (!have[b] || (t[b] >= -NORMAL && t[b] <= NORMAL));
    }
    if (all_normal) {
        HINT("Trims within 10 %: normal.");
    }
    for (int b = 0; b < s->banks; b++) {
        if (!s->bank[b].have_long) {
            HINT("Long-term trims learn over minutes of driving.");
            break;
        }
    }
#undef HINT
    return n;
}
