#ifndef GAUGEBOY_GARAGE_UI_H
#define GAUGEBOY_GARAGE_UI_H

/*
 * GARAGE's screens: layout, touch zones and text, mockup v2
 * (docs/mockups/garage-v2.html). Platform-free so it's unit-tested; drawing is
 * in garage_draw.c (citro2d). The mileage list uses diag_list_* geometry; its
 * Delete question uses diag_dialog_*.
 *
 *   Card: (< name >), [Nickname] [Photo] [Trip] [Mileage Log], (B) Back, (Y) Info
 *   Camera: [Take Photo] (A); the shot: [Use Photo] [Retake] [Remove]
 *   Trip: [Reset Trip]; Mileage log: the entries, (X) New Entry; an entry: [Edit] [Delete]
 */

#include <stdbool.h>

typedef struct {
    int x, y, w, h;
} garage_rect_t;

typedef enum {
    GAR_CARD = 0,
    GAR_CAMERA,
    GAR_SHOT,
    GAR_TRIP,
    GAR_MILES,
    GAR_ENTRY,
    GAR_INFO,
    GAR_KIND_COUNT
} garage_kind_t;

typedef enum {
    GAR_HIT_NONE = 0,
    GAR_HIT_BACK,
    GAR_HIT_RIGHT,       /* (Y) Info on the card and Trip; (X) New Entry on the mileage log */
    GAR_HIT_PREV,        /* the card's half circles: the other cars */
    GAR_HIT_NEXT,
    GAR_HIT_BUTTON,      /* GAR_HIT_BUTTON + i */
} garage_hit_t;

/* The card's buttons, in order. */
enum { GAR_NICKNAME = 0, GAR_PHOTO, GAR_TRIP_BUTTON, GAR_MILES_BUTTON };
/* The shot's buttons. */
enum { GAR_USE = 0, GAR_RETAKE, GAR_REMOVE };

int garage_button_count(garage_kind_t kind);
garage_rect_t garage_rect(garage_kind_t kind, int hit);
int garage_hit(garage_kind_t kind, int px, int py);

/* The card's photo frame on the top screen (the photo drawn into it, 4:3). */
garage_rect_t garage_photo_frame(void);

const char *garage_info_text(garage_kind_t kind);   /* GAR_CARD or GAR_TRIP */

#endif
