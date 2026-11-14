#include <ctype.h>

#include "harness.h"
#include "version.h"

/* GB_VERSION must be plain MAJOR.MINOR.PATCH digits. */
static void test_version_is_semver(void)
{
    const char *v = GB_VERSION;
    int dots = 0;
    int digits_in_part = 0;

    for (; *v != '\0'; v++) {
        if (*v == '.') {
            CHECK(digits_in_part > 0);
            dots++;
            digits_in_part = 0;
        } else {
            CHECK(isdigit((unsigned char)*v));
            digits_in_part++;
        }
    }
    CHECK(digits_in_part > 0);
    CHECK_EQ_INT(dots, 2);
}

static void test_version_matches_parts(void)
{
    char expected[32];
    snprintf(expected, sizeof expected, "%d.%d.%d",
             GB_VERSION_MAJOR, GB_VERSION_MINOR, GB_VERSION_PATCH);
    CHECK_EQ_STR(GB_VERSION, expected);
}

void suite_version(void)
{
    gb_suite("version");
    RUN_TEST(test_version_is_semver);
    RUN_TEST(test_version_matches_parts);
}
