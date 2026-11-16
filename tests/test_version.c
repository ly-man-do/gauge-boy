#include <ctype.h>
#include <string.h>

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

static void test_version_text(void)
{
    /* The app shows GB_VERSION_TEXT; host tests have no build commit. */
    CHECK(strncmp(GB_VERSION_TEXT, GB_VERSION, strlen(GB_VERSION)) == 0);
    CHECK_EQ_STR(GB_VERSION_SUFFIX, "");
}

void suite_version(void)
{
    gb_suite("version");
    RUN_TEST(test_version_is_semver);
    RUN_TEST(test_version_matches_parts);
    RUN_TEST(test_version_text);
}
