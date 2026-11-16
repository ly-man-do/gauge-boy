#include "harness.h"

/* One line per suite; each suite lives in its own tests/test_*.c file. */
void suite_version(void);
void suite_elm327(void);
void suite_transport(void);
void suite_data(void);
void suite_spike(void);
void suite_conntest(void);
void suite_obd(void);
void suite_dtc(void);
void suite_link(void);
void suite_diag(void);
void suite_ui(void);
void suite_pid(void);

int main(void)
{
    suite_version();
    suite_elm327();
    suite_transport();
    suite_data();
    suite_spike();
    suite_conntest();
    suite_obd();
    suite_dtc();
    suite_link();
    suite_diag();
    suite_ui();
    suite_pid();
    return gb_test_summary();
}
