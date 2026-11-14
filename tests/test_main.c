#include "harness.h"

/* One line per suite; each suite lives in its own tests/test_*.c file. */
void suite_version(void);

int main(void)
{
    suite_version();
    return gb_test_summary();
}
