#!/usr/bin/env python3
"""Generate GaugeBoy's HOME menu icon, banner image and banner sound.

All three are original: drawn and synthesized here with the Python standard
library only, no fonts or third-party art. Output goes to meta/:

  meta/icon.png    48x48   HOME menu icon (.3dsx SMDH and .cia)
  meta/banner.png  256x128 top-screen banner shown when the icon is selected
  meta/banner.wav  short chime played with the banner

Run from the repo root:  python3 tools/make_meta_assets.py
"""

import math
import os
import struct
import sys
import wave
import zlib

OUT_DIR = "meta"
SS = 4  # supersampling factor for anti-aliasing

# Palette: carbon black, brushed silver, amber needle, red line.
BG_TOP = (34, 34, 40)
BG_BOTTOM = (12, 12, 16)
FACE = (14, 14, 18)
RING = (196, 198, 204)
RING_DARK = (110, 112, 120)
TICK = (236, 236, 236)
REDLINE = (226, 44, 32)
NEEDLE = (255, 140, 26)
HUB = (180, 182, 188)
TEXT = (240, 240, 240)
ACCENT = (255, 140, 26)


class Canvas:
    """RGB canvas drawn at SS x resolution, then box-filtered down."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.W, self.H = w * SS, h * SS
        self.px = [[(0, 0, 0)] * self.W for _ in range(self.H)]

    def gradient(self, top, bottom):
        for y in range(self.H):
            t = y / (self.H - 1)
            c = tuple(round(a + (b - a) * t) for a, b in zip(top, bottom))
            self.px[y] = [c] * self.W

    def disc(self, cx, cy, r, color):
        cx, cy, r = cx * SS, cy * SS, r * SS
        for y in range(max(0, int(cy - r)), min(self.H, int(cy + r) + 1)):
            row = self.px[y]
            for x in range(max(0, int(cx - r)), min(self.W, int(cx + r) + 1)):
                if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                    row[x] = color

    def polygon(self, pts, color):
        pts = [(x * SS, y * SS) for x, y in pts]
        ys = [p[1] for p in pts]
        for y in range(max(0, int(min(ys))), min(self.H, int(max(ys)) + 1)):
            yc = y + 0.5
            xs = []
            for (x1, y1), (x2, y2) in zip(pts, pts[1:] + pts[:1]):
                if (y1 <= yc < y2) or (y2 <= yc < y1):
                    xs.append(x1 + (yc - y1) * (x2 - x1) / (y2 - y1))
            xs.sort()
            for a, b in zip(xs[0::2], xs[1::2]):
                for x in range(max(0, int(a + 0.5)), min(self.W, int(b + 0.5))):
                    self.px[y][x] = color

    def line(self, x1, y1, x2, y2, width, color):
        dx, dy = x2 - x1, y2 - y1
        n = math.hypot(dx, dy) or 1.0
        ox, oy = -dy / n * width / 2, dx / n * width / 2
        self.polygon([(x1 + ox, y1 + oy), (x2 + ox, y2 + oy),
                      (x2 - ox, y2 - oy), (x1 - ox, y1 - oy)], color)

    def rect(self, x, y, w, h, color):
        self.polygon([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], color)

    def downsample(self):
        out = []
        n = SS * SS
        for y in range(self.h):
            row = []
            for x in range(self.w):
                r = g = b = 0
                for sy in range(SS):
                    src = self.px[y * SS + sy]
                    for sx in range(SS):
                        pr, pg, pb = src[x * SS + sx]
                        r += pr
                        g += pg
                        b += pb
                row.append((round(r / n), round(g / n), round(b / n)))
            out.append(row)
        return out


def draw_gauge(c, cx, cy, r, value):
    """A tachometer: silver bezel, black face, ticks over 270 degrees, red
    line at the top end, amber needle at value (0..1)."""
    c.disc(cx, cy, r, RING_DARK)
    c.disc(cx, cy, r * 0.94, RING)
    c.disc(cx, cy, r * 0.84, FACE)

    start, sweep = math.radians(225), math.radians(270)
    major = 8
    for i in range(major * 2 + 1):
        t = i / (major * 2)
        a = start - sweep * t
        is_major = i % 2 == 0
        r_out = r * 0.78
        r_in = r * (0.60 if is_major else 0.68)
        color = REDLINE if t > 0.76 else TICK
        width = r * (0.075 if is_major else 0.04)
        c.line(cx + r_in * math.cos(a), cy - r_in * math.sin(a),
               cx + r_out * math.cos(a), cy - r_out * math.sin(a), width, color)

    a = start - sweep * value
    tip = (cx + r * 0.74 * math.cos(a), cy - r * 0.74 * math.sin(a))
    tail = (cx - r * 0.16 * math.cos(a), cy + r * 0.16 * math.sin(a))
    c.line(tail[0], tail[1], tip[0], tip[1], r * 0.07, NEEDLE)
    c.disc(cx, cy, r * 0.13, HUB)
    c.disc(cx, cy, r * 0.06, FACE)


# Original 5x7 block font, only the letters the banner needs.
FONT = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "G": ["01111", "10000", "10000", "10011", "10001", "10001", "01111"],
    "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    " ": ["00000"] * 7,
}


def text_width(text, scale):
    return len(text) * 6 * scale - scale


def draw_text(c, x, y, text, scale, color, slant=0.0):
    """Block letters; slant shifts each row right as it goes up (italic)."""
    for i, ch in enumerate(text):
        glyph = FONT[ch]
        gx = x + i * 6 * scale
        for row, bits in enumerate(glyph):
            shift = slant * (6 - row) * scale
            for col, bit in enumerate(bits):
                if bit == "1":
                    px, py = gx + col * scale + shift, y + row * scale
                    c.polygon([(px, py), (px + scale, py),
                               (px + scale - slant * scale, py + scale),
                               (px - slant * scale, py + scale)], color)


def write_png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in rows)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def make_icon():
    c = Canvas(48, 48)
    c.gradient(BG_TOP, BG_BOTTOM)
    draw_gauge(c, 24, 24.5, 22, 0.68)
    return c.downsample()


def make_banner():
    c = Canvas(256, 128)
    c.gradient(BG_TOP, BG_BOTTOM)
    draw_gauge(c, 62, 64, 52, 0.68)

    title = "GAUGEBOY"
    tx = 124
    draw_text(c, tx, 45, title, 2.5, TEXT, slant=0.25)
    c.rect(tx, 66, text_width(title, 2.5), 2, ACCENT)
    draw_text(c, tx, 72, "OBD-II DELUXE", 1.5, ACCENT, slant=0.25)
    return c.downsample()


def make_chime(path):
    """Two quick rising tones with a soft decay, about 0.9 s."""
    rate = 22050
    notes = [(880.0, 0.00), (1318.5, 0.14)]  # A5 then E6
    length = int(rate * 0.9)
    samples = []
    for n in range(length):
        t = n / rate
        v = 0.0
        for freq, start in notes:
            if t >= start:
                dt = t - start
                env = min(1.0, dt / 0.005) * math.exp(-dt * 6.0)
                v += 0.35 * env * (math.sin(2 * math.pi * freq * dt)
                                   + 0.25 * math.sin(4 * math.pi * freq * dt))
        samples.append(max(-32767, min(32767, int(v * 32767))))
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack("<%dh" % len(samples), *samples))


def main():
    if not os.path.isdir("source"):
        sys.exit("run from the repo root")
    os.makedirs(OUT_DIR, exist_ok=True)
    write_png(os.path.join(OUT_DIR, "icon.png"), make_icon())
    write_png(os.path.join(OUT_DIR, "banner.png"), make_banner())
    make_chime(os.path.join(OUT_DIR, "banner.wav"))
    print("wrote meta/icon.png, meta/banner.png, meta/banner.wav")


if __name__ == "__main__":
    main()
