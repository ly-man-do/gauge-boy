#ifndef GAUGEBOY_VERSION_H
#define GAUGEBOY_VERSION_H

/* Semantic version: M0 = 0.0.x, M1 = 0.1.x ... M7 = 1.0.0. */
#define GB_VERSION_MAJOR 0
#define GB_VERSION_MINOR 1
#define GB_VERSION_PATCH 0

#define GB_STR_(x) #x
#define GB_STR(x) GB_STR_(x)

#define GB_VERSION \
    GB_STR(GB_VERSION_MAJOR) "." GB_STR(GB_VERSION_MINOR) "." GB_STR(GB_VERSION_PATCH)

/* The build's git commit, "+62b737a" ("+62b737a*" with uncommitted
 * changes), from build/gb_build.h, which the Makefile writes; "" in the
 * host tests and outside git. */
#if defined(__has_include)
#if __has_include("gb_build.h")
#include "gb_build.h"
#endif
#endif
#ifndef GB_VERSION_SUFFIX
#define GB_VERSION_SUFFIX ""
#endif

/* What the app shows: "0.1.0+62b737a". */
#define GB_VERSION_TEXT GB_VERSION GB_VERSION_SUFFIX

#endif
