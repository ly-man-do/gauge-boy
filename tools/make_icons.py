#!/usr/bin/env python3
"""Draws Gauge Boy's own menu icons (gfx/icons/*.png and gfx/icons.t3s): the shapes from
the HOME mockup (docs/mockups/home-v7.html), on a 24-unit grid with round-capped 2-unit
strokes, anti-aliased by 4x4 supersampling, one PNG per icon and color.

    python3 tools/make_icons.py

Original art (CREDITS.md). Standard library only.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sysui.formats import write_png  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'gfx', 'icons')

RED, ORANGE, YELLOW, BLUE, GREY = (255, 90, 78), (255, 138, 43), (245, 208, 74), (90, 168, 245), (168, 168, 168)
WHITE, DIM, GREEN, AMBER, DARK = (242, 244, 245), (156, 156, 156), (79, 203, 98), (242, 165, 43), (17, 17, 17)


def arc_pts(cx, cy, r, a0, a1, n=48):
    return [(cx + r * math.cos(a0 + (a1 - a0) * i / n), cy + r * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]


def arc_through(p0, p1, r, large):
    """Points of a circular arc from p0 (left) to p1 (right) with radius r, over the top;
    large picks the long way round (center above the chord)."""
    mx, my = (p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2
    half = math.dist(p0, p1) / 2
    h = math.sqrt(max(0.0, r * r - half * half))
    cy = my + h if not large else my - h
    cx = mx
    a0 = math.atan2(p0[1] - cy, p0[0] - cx)
    a1 = math.atan2(p1[1] - cy, p1[0] - cx)
    if a1 < a0:
        a1 += 2 * math.pi   # clockwise on screen (y down): from the left end over the top
    return arc_pts(cx, cy, r, a0, a1)


# Each icon: a list of ops on the 24-unit grid, drawn in order.
#   ('line', [points], width, color)       stroke a polyline, round caps and joins
#   ('loop', [points], width, color)       stroke a closed polyline
#   ('fill', [points], color)              fill a polygon
#   ('disc', (cx, cy, r), color)           fill a circle
#   ('ring', (cx, cy, r), width, color)    stroke a circle
#   ('clear', (cx, cy, r))                 erase a circle (lets the button show through)
def icons():
    gear = [(12, 2.8), (13.6, 5.4), (16.6, 4.8), (17.2, 7.8), (19.8, 9.4), (18.4, 12), (19.8, 14.6),
            (17.2, 16.2), (16.6, 19.2), (13.6, 18.6), (12, 21.2), (10.4, 18.6), (7.4, 19.2), (6.8, 16.2),
            (4.2, 14.6), (5.6, 12), (4.2, 9.4), (6.8, 7.8), (7.4, 4.8), (10.4, 5.4)]
    engine = [(7, 8), (16, 8), (16, 10.5), (18.5, 10.5), (18.5, 8.5), (21, 8.5), (21, 17), (18.5, 17),
              (18.5, 15), (16, 15), (16, 18), (9.5, 18), (7, 15.5)]
    gauge = arc_through((4.2, 17.5), (19.8, 17.5), 9, large=True)

    def live(c):
        return [('line', gauge, 2, c), ('line', [(12, 13.5), (16, 8.5)], 2, c), ('disc', (12, 13.5, 1.6), c),
                ('line', [(6.5, 12.5), (7.8, 12.5)], 2, c), ('line', [(16.2, 12.5), (17.5, 12.5)], 2, c),
                ('line', [(12, 6.8), (12, 8.1)], 2, c)]

    def diag(c):
        return [('loop', engine, 2, c), ('line', [(10, 5), (15, 5)], 2, c), ('line', [(12.5, 5), (12.5, 8)], 2, c),
                ('line', [(3, 10.5), (3, 16.5)], 2, c), ('line', [(3, 13.5), (7, 13.5)], 2, c)]

    def trim(c):
        ops = [('line', [(3.5, y), (20.5, y)], 2, c) for y in (6.5, 12, 17.5)]
        for x, y in ((9, 6.5), (15.5, 12), (7.5, 17.5)):
            ops += [('clear', (x, y, 2.2)), ('ring', (x, y, 2.2), 2, c)]
        return ops

    def track(c):
        return [('fill', [(5, 4), (19, 4), (19, 14), (5, 14)], DARK),
                ('fill', [(5, 4), (9.7, 4), (9.7, 9), (5, 9)], c), ('fill', [(14.3, 4), (19, 4), (19, 9), (14.3, 9)], c),
                ('fill', [(9.7, 9), (14.3, 9), (14.3, 14), (9.7, 14)], c),
                ('loop', [(5, 4), (19, 4), (19, 14), (5, 14)], 2, c), ('line', [(5, 21), (5, 3.5)], 2, c)]

    def log(c):
        return [('line', [(3.5, 20.5), (20.5, 20.5)], 2, c),
                ('line', [(4, 16), (8.5, 10), (12.5, 13.5), (16, 7.5), (20, 11.5)], 2, c)]

    def setup(c):
        return [('loop', gear, 2, c), ('ring', (12, 12, 3), 2, c)]

    def garage(c):
        return [('loop', [(3, 10.5), (12, 4), (21, 10.5), (21, 20), (3, 20)], 2, c),
                ('line', [(7, 20), (7, 13), (17, 13), (17, 20)], 2, c),
                ('line', [(7, 15.5), (17, 15.5)], 2, c), ('line', [(7, 18), (17, 18)], 2, c)]

    def wifi(c, off=False):
        ops = [('line', arc_through((2.5, 9.5), (21.5, 9.5), 14, large=False), 2, c),
               ('line', arc_through((5.5, 13), (18.5, 13), 9.5, large=False), 2, c),
               ('line', arc_through((8.5, 16.4), (15.5, 16.4), 5, large=False), 2, c),
               ('disc', (12, 19.5, 1.3), c)]
        if off:
            ops.append(('line', [(4, 3.5), (20, 20.5)], 2, c))
        return ops

    app = [('fill', gauge + [(19.8, 17.5)], (42, 42, 42)), ('line', gauge + [(4.2, 17.5)], 2, WHITE),
           ('line', [(12, 13.5), (16.6, 8.3)], 2, RED), ('disc', (12, 13.5, 1.8), WHITE)]
    return {
        'live': (30, live(RED)), 'diag': (30, diag(ORANGE)), 'diag_cel': (30, diag(AMBER)),
        'cel_on': (22, diag(AMBER)), 'cel_off': (22, diag((58, 58, 58))),
        'trim': (30, trim(YELLOW)), 'track': (30, track(WHITE)), 'log': (30, log(BLUE)),
        'setup': (30, setup(GREY)),
        'garage': (22, garage(WHITE)), 'garage_dim': (22, garage(DIM)),
        'adapter_on': (22, wifi(GREEN)), 'adapter_off': (22, wifi(RED, off=True)),
        'app': (24, app),
    }


def seg_dist(p, a, b):
    ax, ay, bx, by = a[0], a[1], b[0], b[1]
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0, min(1, ((p[0] - ax) * dx + (p[1] - ay) * dy) / L))
    return math.hypot(p[0] - ax - t * dx, p[1] - ay - t * dy)


def inside(p, poly):
    x, y, c = p[0], p[1], False
    for i in range(len(poly)):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % len(poly)]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def covers(op, p):
    kind = op[0]
    if kind in ('line', 'loop'):
        pts, w = op[1], op[2]
        segs = list(zip(pts, pts[1:])) + ([(pts[-1], pts[0])] if kind == 'loop' else [])
        return min(seg_dist(p, a, b) for a, b in segs) <= w / 2
    if kind == 'fill':
        return inside(p, op[1])
    if kind == 'disc':
        cx, cy, r = op[1]
        return math.hypot(p[0] - cx, p[1] - cy) <= r
    if kind == 'ring':
        cx, cy, r = op[1]
        return abs(math.hypot(p[0] - cx, p[1] - cy) - r) <= op[2] / 2
    if kind == 'clear':
        cx, cy, r = op[1]
        return math.hypot(p[0] - cx, p[1] - cy) <= r
    return False


def draw(size, ops, ss=4):
    scale = 24 / size
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for j in range(ss):
                for i in range(ss):
                    p = ((x + (i + 0.5) / ss) * scale, (y + (j + 0.5) / ss) * scale)
                    col = None
                    for op in ops:
                        if covers(op, p):
                            col = None if op[0] == 'clear' else op[-1]
                    if col:
                        for k in range(3):
                            acc[k] += col[k]
                        acc[3] += 1
            n = acc[3]
            row.append([acc[0] / n, acc[1] / n, acc[2] / n, 255 * n / (ss * ss)] if n else [0, 0, 0, 0])
        rows.append(row)
    return rows


def main():
    os.makedirs(OUT, exist_ok=True)
    names = []
    for name, (size, ops) in icons().items():
        write_png(os.path.join(OUT, name + '.png'), draw(size, ops))
        names.append(name)
    with open(os.path.join(ROOT, 'gfx', 'icons.t3s'), 'w') as f:
        f.write('--atlas -f rgba8888 -z auto --border=edge\n\n' + ''.join('icons/%s.png\n' % n for n in sorted(names)))
    print('%d icons' % len(names))


if __name__ == '__main__':
    main()
