#include "harness.h"

/* One line per suite; each suite lives in its own tests/test_*.c file. */
void suite_version(void);
void suite_elm327(void);
void suite_transport(void);

int main(void)
{
    suite_version();
    suite_elm327();
    suite_transport();
    return gb_test_summary();
}
