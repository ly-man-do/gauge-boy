#include "harness.h"

#include <stdarg.h>

static int tests_run;
static int tests_failed;
static int current_failed;

void gb_check_fail(const char *file, int line, const char *msg)
{
    printf("    %s:%d: %s\n", file, line, msg);
    current_failed = 1;
}

void gb_check_failf(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    printf("    %s:%d: ", file, line);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    current_failed = 1;
}

void gb_suite(const char *name)
{
    printf("%s\n", name);
}

void gb_run_test(const char *name, void (*fn)(void))
{
    current_failed = 0;
    fn();
    tests_run++;
    if (current_failed) {
        tests_failed++;
        printf("  FAIL %s\n", name);
    } else {
        printf("  ok   %s\n", name);
    }
}

int gb_test_summary(void)
{
    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
