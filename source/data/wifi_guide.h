#ifndef GAUGEBOY_WIFI_GUIDE_H
#define GAUGEBOY_WIFI_GUIDE_H

/*
 * The in-app Wi-Fi setup guide (SETUP > Wi-Fi setup guide): the steps for
 * saving the adapter's network in System Settings with the manual IP and
 * DNS GaugeBoy needs (so it can answer the 3DS connection test), with the
 * values worked out from the adapter's address in config.ini and written
 * the way the 3DS shows them (three digits per part). Platform-free.
 */

#include "data/diag_view.h"   /* DIAG_VIEW_COLS */

#define WIFI_GUIDE_STEPS 5

/* Short title of step 0..WIFI_GUIDE_STEPS-1. */
const char *wifi_guide_title(int step);

/* The step's text for the top screen, wrapped to cols, values filled in
 * from adapter_host ("192.168.0.10"). Returns the number of lines. */
int wifi_guide_lines(int step, const char *adapter_host, int cols,
                     char lines[][DIAG_VIEW_COLS + 1], int max);

/* "192.168.0.11" -> "192.168.000.011" (the 3DS's input style). Returns
 * out; copies the text unchanged if it isn't an IPv4 address. */
char *wifi_guide_3digit(const char *ip, char *out, size_t len);

#endif
