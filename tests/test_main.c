#include "harness.h"

/* One line per suite; each suite lives in its own tests/test_*.c file. */
void suite_version(void);
void suite_elm327(void);
void suite_transport(void);
void suite_data(void);
void suite_spike(void);
void suite_conntest(void);

int main(void)
{
    suite_version();
    suite_elm327();
    suite_transport();
    suite_data();
    suite_spike();
    suite_conntest();
    return gb_test_summary();
}
