#!/usr/bin/env python3
"""Makes Gauge Boy's 3DS system UI sprites (gfx/sysui/*.png and gfx/sysui.t3s) from the
owner's own console: the HOME Menu (title 0004003000008F02, v30.0.0) and System Settings
(0004001000021000, v11.0.1), each dumped decrypted with GodMode9 ("NCCH image options",
"Decrypt file"). The dumps are never committed; the sprites are (owner decision, brief
1.22.0; see CREDITS.md).

    python3 tools/make_sysui.py --home HOME.cxi --settings SETTINGS.cxi --ctrtool PATH/ctrtool

What it makes, all for the night theme:
- the HOME Menu status bar pieces in its dark ("WhiteBlack") colors: signal (connected /
  not connected), the connection pill (blue / grey), the battery outline and its cells
  (normal / low), placed and colored as layout HudMenu_00 does
- System Settings' grey bottom bar (TopBase_D_00), its Back / OK corner tabs (Base_D_01),
  its large and medium buttons (B_L, B_M), the half-circle page arrows at the screen
  edges (B_ArrowL, B_ArrowR), rendered from their layouts and recolored to neutral night
  greys (tools/sysui/layout.py; the arrows lose their gold too)
- HOME Menu Settings' OFF / ON toggle (PtBtnT_Abl_00: plain, left half chosen, right half
  chosen, the chosen half and its notch red instead of green) and its scroll bar
  (PtSlideBar: track and grip)
- a pressed ("_dn") version of each touchable piece: the same art, darker

Standard library only, plus ctrtool (3DSGuy/Project_CTR, MIT) to unpack the dumps.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sysui import formats, layout  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'gfx', 'sysui')


def romfs(cxi, ctrtool, tmp):
    d = os.path.join(tmp, os.path.basename(cxi) + '.romfs')
    subprocess.run([ctrtool, '--romfsdir=' + d, cxi], check=True, stdout=subprocess.DEVNULL)
    return d


class Archive:
    def __init__(self, path):
        self.files = formats.archive(open(path, 'rb').read())
        self._tex = {}

    def textures(self):
        return {name: self.texture(name) for name in self.names()}

    def names(self):
        return [p.split('/')[-1][:-6] for p in self.files if p.endswith('.bclim')]

    def texture(self, name):
        if name not in self._tex:
            path = next(p for p in self.files if p.endswith('/' + name + '.bclim'))
            self._tex[name] = formats.Texture(self.files[path])
        return self._tex[name]

    def layout(self, name):
        path = next(p for p in self.files if p.endswith('/' + name + '.bclyt'))
        return layout.Layout(self.files[path])


# ---------- the status bar (hand-placed from HudMenu_00, as the layout does) ----------

def canvas(w, h):
    return [[[0.0, 0.0, 0.0, 0.0] for _ in range(w)] for _ in range(h)]


def tinted(tex, rgb, gloss=False):
    """The texture's brightness takes the color (gloss: lightens toward white instead)."""
    out = []
    for row in tex.rows:
        r = []
        for p in row:
            L = 1.0 if tex.alpha_only else p[0] / 255
            if gloss:
                r.append([c + (255 - c) * L * 0.55 for c in rgb] + [p[3]])
            else:
                r.append([c * L for c in rgb] + [p[3]])
        out.append(r)
    return out


def blit(dst, src, sx, sy, sw, sh, dx, dy, dw=None, dh=None, flip_x=False, flip_y=False):
    dw, dh = dw or sw, dh or sh
    for y in range(dh):
        for x in range(dw):
            u = sx + (x * sw // dw)
            v = sy + (y * sh // dh)
            if flip_x:
                u = sx + sw - 1 - (x * sw // dw)
            if flip_y:
                v = sy + sh - 1 - (y * sh // dh)
            c = src[v][u]
            if c[3] > 0:
                dst[dy + y][dx + x] = list(c)


def status_bar(hud):
    s = {}
    blue, grey = (0x28, 0xB4, 0xF0), (0x5A, 0x5A, 0x5A)
    for name, col in (('sb_signal_on', blue), ('sb_signal_off', grey)):
        c = canvas(23, 16)
        blit(c, tinted(hud.texture('HudNetAtnL_10'), col), 1, 0, 6, 16, 0, 0)
        blit(c, tinted(hud.texture('HudNetAtnR_10'), col), 0, 0, 16, 16, 7, 0)
        s[name] = c
    for name, col in (('sb_pill_on', (0x51, 0xBB, 0xED)), ('sb_pill_off', (0x7A, 0x7A, 0x7A))):
        cap = tinted(hud.texture('HudNetModeL_00'), col, gloss=True)
        c = canvas(108, 16)
        blit(c, cap, 0, 0, 8, 16, 0, 0)
        blit(c, cap, 7, 0, 1, 16, 8, 0, 92, 16)
        blit(c, cap, 0, 0, 8, 16, 100, 0, flip_x=True)
        s[name] = c
    base = tinted(hud.texture('HudBatBase_01'), (0xCD, 0xCD, 0xCD))
    c = canvas(32, 20)
    blit(c, base, 0, 6, 32, 10, 0, 0)
    blit(c, base, 0, 6, 32, 10, 0, 10, flip_y=True)
    s['sb_battery'] = c
    s['sb_cells'] = tinted(hud.texture('HudBat_02'), (0x23, 0xAA, 0xE6))
    s['sb_cells_low'] = tinted(hud.texture('HudBat_02'), (0xF5, 0x7D, 0x41))
    return s


# ---------- System Settings pieces, rendered from their layouts ----------

def render(arc, name, **kw):
    lyt = arc.layout(name)
    used = {mp['tex'] for m in lyt.mats for mp in m['maps']}
    tex = {t: arc.texture(t) for t in used if t in arc.names()}
    return layout.Renderer(lyt, tex, night=True, **kw).render()


def settings_pieces(base, button):
    s = {}
    bar = render(base, 'TopBase_D_00')
    s['bar'] = layout.crop(bar, 0, 202, 320, 240)
    tabs = render(base, 'Base_D_01')
    s['tab_left'] = layout.crop(tabs, 0, 202, 160, 240)
    s['tab_right'] = layout.crop(tabs, 160, 202, 320, 240)
    s['button_large'] = layout.crop(render(button, 'B_L', hide=re.compile(r'N_Invalid')))
    s['button_medium'] = layout.crop(render(button, 'B_M', hide=re.compile(r'N_Invalid')))
    # The page arrows at the screen edges: each layout draws a whole circle and the edge
    # hides half of it, so keep the half that shows (the left arrow's right half).
    for name, lyt, keep_right in (('side_left', 'B_ArrowL', True), ('side_right', 'B_ArrowR', False)):
        full = layout.crop(render(button, lyt, hide=re.compile(r'N_Invalid'), accents=False))
        mid = len(full[0]) // 2
        s[name] = [row[mid:] if keep_right else row[:mid] for row in full]
    return s


# The chosen half of an OFF / ON toggle: the HOME Menu turns it a light green; Gauge Boy
# turns it red, keeping the toggle's own shading (the face's brightness picks the shade).
RED_DARK, RED_LIGHT = (140, 24, 20), (232, 66, 56)


def chosen(plain, left):
    """The plain toggle with one half's face recolored red (corners and edge kept)."""
    out = [[list(p) for p in row] for row in plain]
    h, w = len(plain), len(plain[0])
    half = w // 2
    x0, x1 = (3, half - 1) if left else (half + 1, w - 3)
    y0, y1, r = 3, h - 3, 9
    face = [plain[y][x] for y in range(y0, y1) for x in range(x0, x1)]
    top = max(0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] for p in face) or 1
    for y in range(y0, y1):
        for x in range(x0, x1):
            # round only the outer corners (the divider side stays square)
            cx = x0 + r if left and x < x0 + r else x1 - r if not left and x >= x1 - r else None
            cy = y0 + r if y < y0 + r else y1 - r if y >= y1 - r else None
            if cx is not None and cy is not None and (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 > r * r:
                continue
            p = plain[y][x]
            k = (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) / top
            out[y][x] = [RED_DARK[i] + (RED_LIGHT[i] - RED_DARK[i]) * k for i in range(3)] + [p[3]]
    return out


def lerp_tex(tex, black, white):
    """A texture through a material's black / white blend, recolored for the night."""
    b, w = layout.night_color(black + (255,), False), layout.night_color(white + (255,), False)
    return [[[b[i] + (w[i] - b[i]) * (p[0] / 255) for i in range(3)] + [p[3]] for p in row] for row in tex.rows]


def notch(toggle, petit, left):
    """The chosen half with the HOME Menu's notch (BtnTgl_03, a small down arrow) on its top
    edge, centred. On the 3DS it is part of the chosen face (P_On_00 maps it with the face's
    own textures), so it takes the face's color: red here."""
    out = [list(map(list, row)) for row in toggle]
    tri = petit.texture('BtnTgl_03')
    half = len(out[0]) // 2
    x0 = (half // 2 if left else half + half // 2) - tri.w // 2
    for y in range(tri.h):
        for x in range(tri.w):
            p = tri.rows[y][x]
            a = p[3] / 255
            if a <= 0:
                continue
            k = p[0] / 255
            c = [RED_DARK[i] + (RED_LIGHT[i] - RED_DARK[i]) * k for i in range(3)]
            d = out[y][x0 + x]
            out[y][x0 + x] = [c[i] * a + d[i] * (1 - a) for i in range(3)] + [max(d[3], p[3])]
    return out


def grip(petit, w=20, h=34):
    """The scroll bar's grip (PtSlideBar's SBBtn window and SBBtnEmb ridges), built from its
    corner textures: the renderer doesn't handle this window's four separate frames."""
    top = lerp_tex(petit.texture('SBBtnWndwLT_16'), (0x6E, 0x6E, 0x70), (0xF5, 0xF5, 0xF3))
    bot = lerp_tex(petit.texture('SBBtnWndwLB_16'), (0x6E, 0x6E, 0x70), (0xF5, 0xF5, 0xF3))
    c = len(top)
    out = [[[0, 0, 0, 0] for _ in range(w)] for _ in range(h)]
    for y in range(h):
        for x in range(w):
            src = top if y < h // 2 else bot
            # the corners' outer edges are at the top / left (LT) and bottom / left (LB);
            # the right half mirrors them, the middle stretches their inner row / column
            sy = min(y, c - 1) if y < h // 2 else max(0, c - (h - y))
            sx = min(x, w - 1 - x, c - 1)
            out[y][x] = list(src[sy][sx])
    emb = lerp_tex(petit.texture('SBBtnEmb_8x16'), (0x89, 0x89, 0x8C), (0xE2, 0xE2, 0xE5))
    ex, ey = (w - 8) // 2, (h - 16) // 2
    for y in range(16):
        for x in range(8):
            p = emb[y][x]
            a = p[3] / 255
            d = out[ey + y][ex + x]
            out[ey + y][ex + x] = [p[i] * a + d[i] * (1 - a) for i in range(3)] + [d[3]]
    return out


def pressed(img, k=0.68):
    """A piece as it looks held down: its colors darkened, alpha kept."""
    return [[[p[0] * k, p[1] * k, p[2] * k, p[3]] for p in row] for row in img]


def home_settings_pieces(petit):
    """HOME Menu Settings (the petit archive): the OFF / ON toggle (PtBtnT_Abl_00) and the
    scroll bar (PtSlideBar), for the night theme."""
    s = {}
    plain = re.compile(r'P_IconR')
    s['toggle'] = layout.crop(render(petit, 'PtBtnT_Abl_00', hide=plain, accents=False))
    s['toggle_left'] = notch(chosen(s['toggle'], True), petit, True)
    s['toggle_right'] = notch(chosen(s['toggle'], False), petit, False)
    s['scroll_track'] = layout.crop(render(petit, 'PtSlideBar', hide=re.compile(r'N_Slide_00'), accents=False))
    s['scroll_grip'] = grip(petit)
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--home', required=True, help='decrypted HOME Menu .cxi (0004003000008F02)')
    ap.add_argument('--settings', required=True, help='decrypted System Settings .cxi (0004001000021000)')
    ap.add_argument('--ctrtool', required=True, help='path to ctrtool')
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        home = romfs(a.home, a.ctrtool, tmp)
        settings = romfs(a.settings, a.ctrtool, tmp)
        sprites = status_bar(Archive(os.path.join(home, 'hud_LZ.bin')))
        sprites.update(settings_pieces(Archive(os.path.join(settings, 'base_LZ.bin')),
                                       Archive(os.path.join(settings, 'button_LZ.bin'))))
        sprites.update(home_settings_pieces(Archive(os.path.join(home, 'petit_LZ.bin'))))
    # The pressed look of each touchable piece: the same art, darker (the shape stays exact).
    for n in ('button_large', 'button_medium', 'side_left', 'side_right', 'toggle_left',
              'toggle_right', 'scroll_grip', 'tab_left', 'tab_right'):
        sprites[n + '_dn'] = pressed(sprites[n])
    names = sorted(sprites)
    for n in names:
        formats.write_png(os.path.join(OUT, n + '.png'), sprites[n])
        print('%-16s %dx%d' % (n, len(sprites[n][0]), len(sprites[n])))
    with open(os.path.join(ROOT, 'gfx', 'sysui.t3s'), 'w') as f:
        f.write('--atlas -f rgba8888 -z auto --border=edge\n\n' + ''.join('sysui/%s.png\n' % n for n in names))


if __name__ == '__main__':
    main()
