#ifndef GAUGEBOY_CAR_PHOTO_H
#define GAUGEBOY_CAR_PHOTO_H

/*
 * The Garage card's photo (brief 5e): taken with the 3DS outer camera
 * (400 x 240, RGB565), cropped to 4:3 and halved to 160 x 120, kept on the SD
 * card as a 24-bit BMP per car. Platform-free: the camera itself is
 * ui/camera.c; this is the picture's maths and file format.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAR_PHOTO_W 160
#define CAR_PHOTO_H 120
#define CAR_PHOTO_CAM_W 400   /* the camera's frame: the top screen's size */
#define CAR_PHOTO_CAM_H 240
#define CAR_PHOTO_BMP_SIZE (54 + CAR_PHOTO_W * CAR_PHOTO_H * 3)   /* rows of 480 bytes: no padding */

/* The photo's file name for a car key ("1HG...", "honda/ridgeline/2008"): the
 * characters that can't be in a file name become '_'. "photos/<name>.bmp" is
 * what the app joins to its folder. */
void car_photo_name(const char *key, char *out, size_t len);

/* The middle 320 x 240 of a 400 x 240 camera frame, halved (each pixel the
 * average of four) into 160 x 120. */
void car_photo_crop(const uint16_t *frame, uint16_t *photo);

/* A 160 x 120 RGB565 photo as a 24-bit BMP. Returns the bytes written
 * (CAR_PHOTO_BMP_SIZE), 0 if out is too small. */
size_t car_photo_bmp_write(const uint16_t *photo, uint8_t *out, size_t cap);

/* Reads a BMP written by car_photo_bmp_write (24-bit, 160 x 120, uncompressed).
 * False for anything else. */
bool car_photo_bmp_read(const uint8_t *data, size_t len, uint16_t *photo);

/* Copies a w x h RGB565 image into a GPU texture's tiled layout (8 x 8 tiles
 * in Morton order), the texture tw wide (a power of two >= w). The image's
 * first row is the texture's first (its top as kit_pic draws it; checked in
 * Azahar with a test photo). */
void car_photo_tile(const uint16_t *src, int w, int h, uint16_t *dst, int tw, int th);

#endif
