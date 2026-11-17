#include "history.h"

#include <string.h>

static history_series_t *series(history_t *h, uint8_t pid)
{
    for (int i = 0; i < h->count; i++) {
        if (h->s[i].pid == pid) {
            return &h->s[i];
        }
    }
    return NULL;
}

static const history_series_t *series_c(const history_t *h, uint8_t pid)
{
    return series((history_t *)h, pid);
}

void history_reset(history_t *h, const uint8_t *pids, int n)
{
    h->count = 0;
    for (int i = 0; i < n && h->count < OBD_LIVE_MAX; i++) {
        if (series(h, pids[i]) != NULL) {
            continue;
        }
        history_series_t *s = &h->s[h->count++];
        s->pid = pids[i];
        s->head = 0;
        s->count = 0;
        s->last_ms = 0;
    }
}

bool history_add(history_t *h, uint8_t pid, uint64_t t_ms, float v)
{
    history_series_t *s = series(h, pid);
    if (s == NULL) {
        return false;
    }
    if (s->count > 0 && t_ms <= s->last_ms) {
        return false;
    }
    s->t_ms[s->head] = t_ms;
    s->v[s->head] = v;
    s->head = (s->head + 1) % HISTORY_LEN;
    if (s->count < HISTORY_LEN) {
        s->count++;
    }
    s->last_ms = t_ms;
    return true;
}

int history_from_live(history_t *h, const obd_live_t *lv)
{
    int added = 0;
    for (int i = 0; i < lv->count; i++) {
        const obd_live_pid_t *p = &lv->pid[i];
        if (p->have && history_add(h, p->pid, p->at_ms, p->value)) {
            added++;
        }
    }
    return added;
}

bool history_get(const history_t *h, uint8_t pid, int i, uint64_t *t_ms, float *v)
{
    const history_series_t *s = series_c(h, pid);
    if (s == NULL || i < 0 || i >= s->count) {
        return false;
    }
    int k = (s->head - s->count + i + HISTORY_LEN) % HISTORY_LEN;
    if (t_ms != NULL) {
        *t_ms = s->t_ms[k];
    }
    if (v != NULL) {
        *v = s->v[k];
    }
    return true;
}

int history_count(const history_t *h, uint8_t pid)
{
    const history_series_t *s = series_c(h, pid);
    return s != NULL ? s->count : 0;
}

history_stats_t history_window(const history_t *h, uint8_t pid, uint64_t from_ms,
                               uint64_t to_ms)
{
    history_stats_t st = { 0, 0.0f, 0.0f, 0.0f };
    double sum = 0.0;
    int n = history_count(h, pid);
    for (int i = 0; i < n; i++) {
        uint64_t t;
        float v;
        history_get(h, pid, i, &t, &v);
        if (t < from_ms || t > to_ms) {
            continue;
        }
        if (st.n == 0) {
            st.min = st.max = v;
        }
        st.min = v < st.min ? v : st.min;
        st.max = v > st.max ? v : st.max;
        sum += v;
        st.n++;
    }
    st.avg = st.n > 0 ? (float)(sum / st.n) : 0.0f;
    return st;
}

void history_columns(const history_t *h, uint8_t pid, uint64_t from_ms, uint64_t to_ms,
                     int ncols, graph_cols_t *g)
{
    graph_cols_init(g, 0.0, (double)(to_ms - from_ms) / 1000.0, ncols);
    int n = history_count(h, pid);
    for (int i = 0; i < n; i++) {
        uint64_t t;
        float v;
        history_get(h, pid, i, &t, &v);
        if (t >= from_ms && t <= to_ms) {
            graph_cols_add(g, (double)(t - from_ms) / 1000.0, v);
        }
    }
}
