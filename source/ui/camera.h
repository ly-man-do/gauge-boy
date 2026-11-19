#ifndef GAUGEBOY_CAMERA_H
#define GAUGEBOY_CAMERA_H

/*
 * The 3DS outer camera (the right one), 400 x 240 RGB565 frames for the
 * Garage card's photo (3DS only; the picture's maths is data/car_photo.c).
 * Never blocks: camera_poll returns false until a frame has arrived.
 */

#include <stdbool.h>
#include <stdint.h>

#include "data/car_photo.h"

/* Turns the camera on. False if it can't (no camera service). */
bool camera_start(void);

/* Copies the newest frame (CAR_PHOTO_CAM_W x CAR_PHOTO_CAM_H) into frame and
 * asks for the next one. False if none has arrived since the last call. */
bool camera_poll(uint16_t *frame);

/* Turns it off (safe to call when it's off). */
void camera_stop(void);

#endif
