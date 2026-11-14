#ifndef GAUGEBOY_TEST_HARNESS_H
#define GAUGEBOY_TEST_HARNESS_H

/*
 * Minimal host test harness (no third-party framework).
 *
 * Write a test as `static void test_name(void)` using the CHECK macros,
 * group tests in a suite function `void suite_name(void)` that calls
 * RUN_TEST for each one, and add the suite to tests/test_main.c.
 * A failed CHECK reports the location and keeps running the test.
 */

#include <stdio.h>
#include <string.h>

void gb_check_fail(const char *file, int line, const char *msg);
/* printf-style failure message; marks the current test failed. */
void gb_check_failf(const char *file, int line, const char *fmt, ...);
void gb_run_test(const char *name, void (*fn)(void));
void gb_suite(const char *name);
int gb_test_summary(void);

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) gb_check_fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                  \
    do {                                                                \
        long long gb_a_ = (long long)(actual);                          \
        long long gb_e_ = (long long)(expected);                        \
        if (gb_a_ != gb_e_) {                                           \
            gb_check_failf(__FILE__, __LINE__, "%s == %lld, expected %lld", \
                           #actual, gb_a_, gb_e_);                      \
        }                                                               \
    } while (0)

#define CHECK_EQ_STR(actual, expected)                                  \
    do {                                                                \
        const char *gb_a_ = (actual);                                   \
        const char *gb_e_ = (expected);                                 \
        if (gb_a_ == NULL || gb_e_ == NULL || strcmp(gb_a_, gb_e_) != 0) { \
            gb_check_failf(__FILE__, __LINE__, "%s == \"%s\", expected \"%s\"", \
                           #actual, gb_a_ ? gb_a_ : "(null)",           \
                           gb_e_ ? gb_e_ : "(null)");                   \
        }                                                               \
    } while (0)

#define RUN_TEST(fn) gb_run_test(#fn, fn)

#endif
