/* History rings and graph columns (source/data/history, graph). */
#include <string.h>

#include "harness.h"
#include "data/graph.h"
#include "data/history.h"

static history_t h;

static void test_history_ring(void)
{
    static const uint8_t pids[] = { 0x0C, 0x0D, 0x0C };
    history_reset(&h, pids, 3);
    CHECK_EQ_INT(h.count, 2);                            /* repeats dropped */
    CHECK(!history_add(&h, 0x05, 10, 1.0f));             /* not polled */
    for (int i = 0; i < HISTORY_LEN + 10; i++) {
        CHECK(history_add(&h, 0x0C, (uint64_t)(i + 1) * 100, (float)i));
    }
    CHECK(!history_add(&h, 0x0C, 50, 9.0f));             /* older than the last */
    CHECK_EQ_INT(history_count(&h, 0x0C), HISTORY_LEN);  /* the oldest 10 dropped */
    uint64_t t;
    float v;
    CHECK(history_get(&h, 0x0C, 0, &t, &v) && v == 10.0f && t == 1100);
    CHECK(history_get(&h, 0x0C, HISTORY_LEN - 1, &t, &v) && v == (float)(HISTORY_LEN + 9));
    CHECK(!history_get(&h, 0x0C, HISTORY_LEN, &t, &v));
    CHECK_EQ_INT(history_count(&h, 0x0D), 0);

    history_stats_t st = history_window(&h, 0x0C, 1100, 1500);   /* values 10..14 */
    CHECK_EQ_INT(st.n, 5);
    CHECK(st.min == 10.0f && st.max == 14.0f && st.avg == 12.0f);
}

static void test_history_from_live(void)
{
    static obd_live_t lv;
    memset(&lv, 0, sizeof lv);
    lv.count = 2;
    lv.pid[0].pid = 0x0C;
    lv.pid[1].pid = 0x0D;
    static const uint8_t pids[] = { 0x0C, 0x0D };
    history_reset(&h, pids, 2);
    lv.pid[0].have = true; lv.pid[0].value = 800.0f; lv.pid[0].at_ms = 100;
    CHECK_EQ_INT(history_from_live(&h, &lv), 1);          /* 0D not read yet */
    CHECK_EQ_INT(history_from_live(&h, &lv), 0);          /* nothing new */
    lv.pid[0].value = 820.0f; lv.pid[0].at_ms = 160;
    lv.pid[1].have = true; lv.pid[1].value = 5.0f; lv.pid[1].at_ms = 150;
    CHECK_EQ_INT(history_from_live(&h, &lv), 2);
    CHECK_EQ_INT(history_count(&h, 0x0C), 2);

    graph_cols_t g;
    history_columns(&h, 0x0C, 100, 200, 10, &g);
    CHECK(g.any && g.n == 2 && g.vmin == 800.0f && g.vmax == 820.0f);
    CHECK(g.have[0] && g.have[6] && !g.have[3]);          /* 0 s and 0.06 s of 0.1 s */
}

static void test_graph_columns(void)
{
    graph_cols_t g;
    graph_cols_init(&g, 0.0, 10.0, 5);                   /* 2 s a column */
    for (int i = 0; i <= 100; i++) {
        graph_cols_add(&g, i * 0.1, i == 33 ? 99.0f : 1.0f);   /* one spike at 3.3 s */
    }
    graph_cols_add(&g, 11.0, 50.0f);                     /* outside: ignored */
    CHECK_EQ_INT((int)g.n, 101);
    CHECK(g.have[1] && g.max[1] == 99.0f && g.min[1] == 1.0f);   /* the spike survives */
    CHECK(g.max[0] == 1.0f && g.max[4] == 1.0f);
    CHECK(g.vmin == 1.0f && g.vmax == 99.0f);
    CHECK(graph_cols_avg(&g) > 1.9f && graph_cols_avg(&g) < 2.0f);
    graph_cols_init(&g, 0.0, 1.0, 10000);
    CHECK_EQ_INT(g.ncols, GRAPH_MAX_COLS);

    CHECK_EQ_INT(graph_y(0.0f, 0.0f, 100.0f, 101), 100);   /* bottom */
    CHECK_EQ_INT(graph_y(100.0f, 0.0f, 100.0f, 101), 0);   /* top */
    CHECK_EQ_INT(graph_y(50.0f, 0.0f, 100.0f, 101), 50);
    CHECK_EQ_INT(graph_y(500.0f, 0.0f, 100.0f, 101), 0);   /* clamped */

    float lo, hi, step;
    graph_scale(712.0f, 3488.0f, &lo, &hi, &step);
    CHECK(step == 1000.0f && lo == 0.0f && hi == 4000.0f);
    graph_scale(-4.7f, 3.1f, &lo, &hi, &step);
    CHECK(step == 2.0f && lo == -6.0f && hi == 4.0f);
    graph_scale(14.1f, 14.1f, &lo, &hi, &step);          /* flat line */
    CHECK(lo <= 14.1f && hi >= 14.1f && hi > lo);
}

void suite_history(void)
{
    RUN_TEST(test_history_ring);
    RUN_TEST(test_history_from_live);
    RUN_TEST(test_graph_columns);
}
