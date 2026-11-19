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
  and its large and medium buttons (B_L, B_M), rendered from their layouts and recolored
  to neutral night greys (tools/sysui/layout.py)

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
    names = sorted(sprites)
    for n in names:
        formats.write_png(os.path.join(OUT, n + '.png'), sprites[n])
        print('%-16s %dx%d' % (n, len(sprites[n][0]), len(sprites[n])))
    with open(os.path.join(ROOT, 'gfx', 'sysui.t3s'), 'w') as f:
        f.write('--atlas -f rgba8888 -z auto --border=edge\n\n' + ''.join('sysui/%s.png\n' % n for n in names))


if __name__ == '__main__':
    main()
