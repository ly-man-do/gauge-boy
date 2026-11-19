#ifndef GAUGEBOY_HOME_TOP_H
#define GAUGEBOY_HOME_TOP_H

/*
 * HOME top screen (400x240): what it says. Platform-free so the text is
 * unit-tested; drawing is in home_draw.c (citro2d).
 *
 *   [signal] (Connected)                    11/18 (Wed)  21:12 [battery]
 *                 (o) GAUGE BOY: OBD-II DELUXE
 *   . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
 *   | [Car       ] Big Red   2008 Honda Ridgeline                      |
 *   | [CEL       ] o Off                                               |
 *   | [Codes     ] None stored                                         |
 *   | [Odometer  ] 123,456 mi            (big LCD digits)              |
 *   | [Adapter   ] ELM327 v2.3   ISO 9141-2                            |
 *
 * Mockup v7 (docs/mockups/home-v7.html), owner-approved 2026-11-18.
 */

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool connected;
    bool have_car;
    const char *nickname;      /* "" if none */
    const char *title;         /* "2008 Honda Ridgeline" */
    bool cel_known, cel_on;
    bool have_mil_km;          /* distance driven with the CEL on */
    int mil_km;
    int codes;                 /* stored codes, -1 if not read */
    const char *code_list;     /* "P0171, P0174" (stored codes), "" if none */
    int protocol;              /* ELM327 protocol 1-12, 0 if unknown */
    const char *adapter_id;    /* "ELM327 V2.3", "" if unknown */
    const char *adapter_host;  /* "192.168.0.10", shown when not connected */
    bool metric;
    bool have_odo;
    double odo_km;
} home_top_t;

typedef enum {
    HOME_TONE_NORMAL = 0,      /* white */
    HOME_TONE_DIM,             /* not known yet: grey */
    HOME_TONE_RED,             /* not connected */
    HOME_TONE_AMBER,           /* the CEL is on */
} home_tone_t;

#define HOME_ROWS 5
#define HOME_ROW_TEXT 48

typedef struct {
    const char *tag;                /* "Car", "CEL" ... */
    char value[HOME_ROW_TEXT];      /* "Big Red"; for an LCD row the digits ("123,456") */
    char detail[HOME_ROW_TEXT];     /* "2008 Honda Ridgeline", "mi", "" */
    home_tone_t tone;
    bool lcd;                       /* value in LCD digits */
    bool lamp;                      /* a CEL lamp before the value */
} home_row_t;

/* The info panel's five rows: Car, CEL, Codes, Odometer, Adapter. */
void home_top_rows(const home_top_t *t, home_row_t rows[HOME_ROWS]);

/* Short protocol name for ELM327 protocol numbers 1-12, "" otherwise. */
const char *home_protocol_name(int protocol);

/* Digits with thousands commas: 123456 -> "123,456". */
void home_group_digits(long v, char *out, size_t len);

#endif
