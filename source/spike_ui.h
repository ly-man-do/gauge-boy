#ifndef GAUGEBOY_SPIKE_UI_H
#define GAUGEBOY_SPIKE_UI_H

/*
 * M0 spike app (3DS): log on the top screen, touch buttons on the bottom,
 * adapter I/O on a worker thread. Returns when the user exits.
 * Call after gfxInitDefault().
 */
void spike_ui_run(void);

#endif
