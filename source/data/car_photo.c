#include "car_photo.h"

#include <stdio.h>
#include <string.h>

void car_photo_name(const char *key, char *out, size_t len)
{
    if (len == 0) {
        return;
    }
    size_t n = 0;
    for (const char *p = key; *p != '\0' && n + 1 < len; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '-';
        out[n++] = ok ? c : '_';
    }
    out[n] = '\0';
}

static void unpack(uint16_t p, unsigned *r, unsigned *g, unsigned *b)
{
    *r = (p >> 11) & 0x1F;
    *g = (p >> 5) & 0x3F;
    *b = p & 0x1F;
}

static uint16_t pack(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void car_photo_crop(const uint16_t *frame, uint16_t *photo)
{
    const int x0 = (CAR_PHOTO_CAM_W - CAR_PHOTO_CAM_H * 4 / 3) / 2;   /* 40 */
    for (int y = 0; y < CAR_PHOTO_H; y++) {
        for (int x = 0; x < CAR_PHOTO_W; x++) {
            unsigned rs = 0, gs = 0, bs = 0;
            for (int k = 0; k < 4; k++) {
                int sx = x0 + x * 2 + (k & 1), sy = y * 2 + (k >> 1);
                unsigned r, g, b;
                unpack(frame[sy * CAR_PHOTO_CAM_W + sx], &r, &g, &b);
                rs += r;
                gs += g;
                bs += b;
            }
            photo[y * CAR_PHOTO_W + x] = pack((rs + 2) / 4, (gs + 2) / 4, (bs + 2) / 4);
        }
    }
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t car_photo_bmp_write(const uint16_t *photo, uint8_t *out, size_t cap)
{
    if (cap < CAR_PHOTO_BMP_SIZE) {
        return 0;
    }
    memset(out, 0, 54);
    out[0] = 'B';
    out[1] = 'M';
    put32(out + 2, CAR_PHOTO_BMP_SIZE);
    put32(out + 10, 54);                 /* pixels start */
    put32(out + 14, 40);                 /* BITMAPINFOHEADER */
    put32(out + 18, CAR_PHOTO_W);
    put32(out + 22, CAR_PHOTO_H);        /* positive: rows bottom-up */
    out[26] = 1;                         /* planes */
    out[28] = 24;                        /* bits a pixel */
    put32(out + 34, CAR_PHOTO_W * CAR_PHOTO_H * 3);
    put32(out + 38, 2835);               /* 72 dpi */
    put32(out + 42, 2835);
    uint8_t *px = out + 54;
    for (int row = 0; row < CAR_PHOTO_H; row++) {
        const uint16_t *src = photo + (CAR_PHOTO_H - 1 - row) * CAR_PHOTO_W;
        for (int x = 0; x < CAR_PHOTO_W; x++) {
            unsigned r, g, b;
            unpack(src[x], &r, &g, &b);
            *px++ = (uint8_t)((b << 3) | (b >> 2));
            *px++ = (uint8_t)((g << 2) | (g >> 4));
            *px++ = (uint8_t)((r << 3) | (r >> 2));
        }
    }
    return CAR_PHOTO_BMP_SIZE;
}

bool car_photo_bmp_read(const uint8_t *data, size_t len, uint16_t *photo)
{
    if (len < CAR_PHOTO_BMP_SIZE || data[0] != 'B' || data[1] != 'M' ||
        get32(data + 18) != CAR_PHOTO_W || get32(data + 22) != CAR_PHOTO_H || data[28] != 24 ||
        get32(data + 30) != 0) {
        return false;
    }
    uint32_t off = get32(data + 10);
    if (off > len || len - off < (size_t)CAR_PHOTO_W * CAR_PHOTO_H * 3) {
        return false;
    }
    const uint8_t *px = data + off;
    for (int row = 0; row < CAR_PHOTO_H; row++) {
        uint16_t *dst = photo + (CAR_PHOTO_H - 1 - row) * CAR_PHOTO_W;
        for (int x = 0; x < CAR_PHOTO_W; x++) {
            unsigned b = px[0] >> 3, g = px[1] >> 2, r = px[2] >> 3;
            dst[x] = pack(r, g, b);
            px += 3;
        }
    }
    return true;
}

/* The index of (x, y) inside an 8 x 8 tile (Morton order). */
static int morton(int x, int y)
{
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) |
           ((y & 4) << 3);
}

void car_photo_tile(const uint16_t *src, int w, int h, uint16_t *dst, int tw, int th)
{
    (void)th;
    for (int y = 0; y < h; y++) {
        int ty = y;   /* with citro2d's sub-texture (top 1.0) the first rows are the top */
        for (int x = 0; x < w; x++) {
            int tile = (ty / 8) * (tw / 8) + x / 8;
            dst[tile * 64 + morton(x & 7, ty & 7)] = src[y * w + x];
        }
    }
}
