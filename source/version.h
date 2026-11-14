#ifndef GAUGEBOY_VERSION_H
#define GAUGEBOY_VERSION_H

/* Semantic version: M0 = 0.0.x, M1 = 0.1.x ... M7 = 1.0.0. */
#define GB_VERSION_MAJOR 0
#define GB_VERSION_MINOR 0
#define GB_VERSION_PATCH 1

#define GB_STR_(x) #x
#define GB_STR(x) GB_STR_(x)

#define GB_VERSION \
    GB_STR(GB_VERSION_MAJOR) "." GB_STR(GB_VERSION_MINOR) "." GB_STR(GB_VERSION_PATCH)

#endif
