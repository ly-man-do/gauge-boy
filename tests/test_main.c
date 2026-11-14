#include "harness.h"

/* One line per suite; each suite lives in its own tests/test_*.c file. */
void suite_version(void);
void suite_elm327(void);

int main(void)
{
    suite_version();
    suite_elm327();
    return gb_test_summary();
}
