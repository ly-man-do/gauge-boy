"""3DS layouts (BCLYT) for the system UI pipeline (make_sysui.py): parsing and a small
software renderer. Standard library only.

The renderer draws picture and window panes the way the layout engine does for simple
materials: anchor points, scale (negative = mirrored), UV wrap / mirror, vertex colors,
and each material's black / white color blend by texture brightness (alpha-only textures
read as black, as on the 3DS GPU). Materials that combine several textures use the
largest one. Text panes are skipped: the app draws its labels with the system font.

`night=True` recolors for Gauge Boy's night theme: low-saturation surface colors become
neutral greys along the HOME Menu's dark-theme curve, bright accents keep their color
(`accents=False` greys them too), and vertex shading keeps its brightness but loses its
tint.
"""
import math
import struct


def _cstr(b):
    return b.split(b'\0')[0].decode('ascii', 'replace')


def _rgba(b):
    return list(b[:4])


class Layout:
    def __init__(self, data):
        assert data[:4] == b'CLYT', 'not a BCLYT'
        p = struct.unpack_from('<H', data, 6)[0]
        nsec = struct.unpack_from('<I', data, 0x10)[0]
        self.tex, self.mats, self.root = [], [], None
        stack, last = [], None
        for _ in range(nsec):
            m = data[p:p + 4]
            size = struct.unpack_from('<I', data, p + 4)[0]
            s = data[p:p + size]
            if m == b'txl1':
                n = struct.unpack_from('<I', s, 8)[0]
                self.tex = [_cstr(s[12 + o:])[:-6] for o in struct.unpack_from('<%dI' % n, s, 12)]
            elif m == b'mat1':
                n = struct.unpack_from('<I', s, 8)[0]
                for o in struct.unpack_from('<%dI' % n, s, 12):
                    flags = struct.unpack_from('<I', s, o + 48)[0]
                    nt, ntm = flags & 3, (flags >> 2) & 3
                    maps = []
                    for k in range(nt):
                        idx, a, b = struct.unpack_from('<HBB', s, o + 52 + 4 * k)
                        maps.append({'tex': self.tex[idx], 'ws': a & 3, 'wt': b & 3})
                    q = o + 52 + 4 * nt
                    srt = [list(struct.unpack_from('<5f', s, q + 20 * k)) for k in range(ntm)]
                    self.mats.append({'name': _cstr(s[o:o + 20]), 'black': _rgba(s[o + 20:]),
                                      'white': _rgba(s[o + 24:]), 'maps': maps, 'srt': srt})
            elif m in (b'pan1', b'pic1', b'txt1', b'wnd1', b'bnd1'):
                pane = {'type': m.decode()[:3], 'name': _cstr(s[12:28]), 'vis': s[8] & 1,
                        'alpha': s[10], 'origin': s[9],
                        'pos': list(struct.unpack_from('<3f', s, 0x24)),
                        'rot': list(struct.unpack_from('<3f', s, 0x30)),
                        'scale': list(struct.unpack_from('<2f', s, 0x3C)),
                        'size': list(struct.unpack_from('<2f', s, 0x44)), 'kids': []}
                if m == b'pic1':
                    pane['vtx'] = [_rgba(s[0x4C + 4 * k:]) for k in range(4)]
                    pane['mat'] = struct.unpack_from('<H', s, 0x5C)[0]
                    nuv = struct.unpack_from('<H', s, 0x5E)[0]
                    pane['uv'] = [list(struct.unpack_from('<8f', s, 0x60 + 32 * k)) for k in range(nuv)]
                elif m == b'wnd1':
                    pane['inflate'] = list(struct.unpack_from('<4f', s, 0x4C))
                    nfr = s[0x5C]
                    coff, foff = struct.unpack_from('<II', s, 0x60)
                    pane['vtx'] = [_rgba(s[coff + 4 * k:]) for k in range(4)]
                    pane['mat'] = struct.unpack_from('<H', s, coff + 16)[0]
                    nuv = struct.unpack_from('<H', s, coff + 18)[0]
                    pane['uv'] = [list(struct.unpack_from('<8f', s, coff + 20 + 32 * k)) for k in range(nuv)]
                    pane['frames'] = []
                    for k in range(nfr):
                        fo = struct.unpack_from('<I', s, foff + 4 * k)[0]
                        mi, flip = struct.unpack_from('<HB', s, fo)
                        pane['frames'].append({'mat': mi, 'flip': flip})
                if self.root is None:
                    self.root = pane
                elif stack:
                    stack[-1]['kids'].append(pane)
                last = pane
            elif m == b'pas1':
                stack.append(last)
            elif m == b'pae1':
                stack.pop()
            p += size

    def find(self, name, pane=None):
        pane = pane or self.root
        if pane['name'] == name:
            return pane
        for k in pane['kids']:
            r = self.find(name, k)
            if r:
                return r
        return None


# ---------- night recolor ----------

def _lum(c):
    return (0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]) / 255


def _sat(c):
    mx, mn = max(c[:3]), min(c[:3])
    return (mx - mn) / mx if mx else 0


def night_color(c, accents=True):
    """A surface color for the night theme (accents keep theirs unless accents=False)."""
    L = _lum(c)
    if accents and _sat(c) > 0.45 and L > 0.35:
        return list(c)
    v = 255 * 0.55 * L ** 1.6
    return [v, v, v, c[3]]


def night_shade(c, accents=True):
    """A vertex (shading) color without its tint."""
    if accents and _sat(c) > 0.45 and max(c[:3]) > 150:
        return list(c)
    L = _lum(c) * 255
    return [L, L, L, c[3]]


# ---------- renderer ----------

def _mul(A, B):
    return [A[0] * B[0] + A[2] * B[1], A[1] * B[0] + A[3] * B[1],
            A[0] * B[2] + A[2] * B[3], A[1] * B[2] + A[3] * B[3],
            A[0] * B[4] + A[2] * B[5] + A[4], A[1] * B[4] + A[3] * B[5] + A[5]]


def _apply(M, p):
    return (M[0] * p[0] + M[2] * p[1] + M[4], M[1] * p[0] + M[3] * p[1] + M[5])


def _invert(M):
    det = M[0] * M[3] - M[1] * M[2]
    return [M[3] / det, -M[1] / det, -M[2] / det, M[0] / det,
            (M[2] * M[5] - M[3] * M[4]) / det, (M[1] * M[4] - M[0] * M[5]) / det]


def _wrap(x, n, mode):
    if mode == 1:
        return x % n
    if mode == 2:
        p = x % (2 * n)
        return p if p < n else 2 * n - 1 - p
    return max(0, min(n - 1, x))


def _bilerp(c, s, t):
    out = []
    for k in range(4):
        top = c[0][k] + (c[1][k] - c[0][k]) * s
        bot = c[2][k] + (c[3][k] - c[2][k]) * s
        out.append(top + (bot - top) * t)
    return out


def _uv_at(uv, s, t):
    tu = uv[0] + (uv[2] - uv[0]) * s
    tv = uv[1] + (uv[3] - uv[1]) * s
    bu = uv[4] + (uv[6] - uv[4]) * s
    bv = uv[5] + (uv[7] - uv[5]) * s
    return tu + (bu - tu) * t, tv + (bv - tv) * t


def _rect(p):
    w, h = p['size']
    col, row = p['origin'] % 3, p['origin'] // 3
    x0 = 0 if col == 0 else -w / 2 if col == 1 else -w
    y0 = 0 if row == 0 else -h / 2 if row == 1 else -h
    return [x0, y0, x0 + w, y0 + h]


class Renderer:
    """Renders a Layout into an RGBA float buffer the size of its root pane."""

    def __init__(self, layout, textures, night=False, hide=None, show=None, accents=True, colors=None):
        self.lyt, self.tex, self.hide, self.show = layout, textures, hide, show or {}
        self.mats = layout.mats
        if night:
            self.mats = [dict(m, black=night_color(m['black'], accents), white=night_color(m['white'], accents))
                         for m in layout.mats]
        self.night, self.accents = night, accents
        for i, m in enumerate(self.mats):   # colors={material: (black, white)}, after the recolor
            if colors and m['name'] in colors:
                self.mats[i] = dict(m, black=list(colors[m['name']][0]), white=list(colors[m['name']][1]))
        self.w, self.h = int(layout.root['size'][0]), int(layout.root['size'][1])
        self.buf = [[[0.0, 0.0, 0.0, 0.0] for _ in range(self.w)] for _ in range(self.h)]

    def primary(self, mat):
        best, area = -1, -1
        for k, mp in enumerate(mat['maps']):
            t = self.tex.get(mp['tex'])
            if t and t.w * t.h > area:
                best, area = k, t.w * t.h
        return best

    def sample(self, mat, u, v, k):
        W, B = mat['white'], mat['black']
        if k < 0:
            return list(W)
        mp = mat['maps'][k]
        t = self.tex[mp['tex']]
        if k < len(mat['srt']):
            srt = mat['srt'][k]
            u, v = u * srt[3] + srt[0], v * srt[4] - srt[1]
        x = _wrap(math.floor(u * t.w), t.w, mp['ws'])
        y = _wrap(math.floor(v * t.h), t.h, mp['wt'])
        px = t.rows[y][x]
        L = 0.0 if t.alpha_only else px[0] / 255
        A = px[3] / 255
        return [B[0] + (W[0] - B[0]) * L, B[1] + (W[1] - B[1]) * L, B[2] + (W[2] - B[2]) * L,
                B[3] + (W[3] - B[3]) * A]

    def quad(self, M, rect, fn):
        pts = [_apply(M, p) for p in ((rect[0], rect[1]), (rect[2], rect[1]), (rect[0], rect[3]), (rect[2], rect[3]))]
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        x0, x1 = max(0, math.floor(min(xs))), min(self.w, math.ceil(max(xs)))
        y0, y1 = max(0, math.floor(min(ys))), min(self.h, math.ceil(max(ys)))
        inv = _invert(M)
        rw, rh = rect[2] - rect[0], rect[3] - rect[1]
        if rw == 0 or rh == 0:
            return
        for y in range(y0, y1):
            row = self.buf[y]
            for x in range(x0, x1):
                lx, ly = _apply(inv, (x + 0.5, y + 0.5))
                s, t = (lx - rect[0]) / rw, (ly - rect[1]) / rh
                if not (0 <= s < 1 and 0 <= t < 1):
                    continue
                c = fn(s, t)
                if not c or c[3] <= 0:
                    continue
                a = min(1.0, c[3] / 255)
                d = row[x]
                da = d[3] / 255
                oa = a + da * (1 - a)
                for k in range(3):
                    d[k] = (c[k] * a + d[k] * da * (1 - a)) / oa if oa else 0
                d[3] = oa * 255

    def render(self):
        self._draw(self.lyt.root, [1, 0, 0, 1, self.w / 2, self.h / 2], 1.0)
        return self.buf

    def _shade(self, vtx):
        return [night_shade(c, self.accents) for c in vtx] if self.night else vtx

    def _draw(self, p, M, alpha):
        if p['name'] in self.show:
            p = dict(p, **self.show[p['name']])
        if not p['vis'] or p['type'] == 'txt' or (self.hide and self.hide.match(p['name'])):
            return
        T = [1, 0, 0, 1, p['pos'][0], -p['pos'][1]]
        r = -p['rot'][2] * math.pi / 180
        R = [math.cos(r), math.sin(r), -math.sin(r), math.cos(r), 0, 0]
        S = [p['scale'][0], 0, 0, p['scale'][1], 0, 0]
        Mp = _mul(_mul(_mul(M, T), R), S)
        a = alpha * p['alpha'] / 255
        rect = _rect(p)
        if p['type'] == 'pic':
            mat = self.mats[p['mat']]
            k = self.primary(mat)
            uv = p['uv'][max(0, k)] if len(p['uv']) > max(0, k) else (p['uv'][0] if p['uv'] else [0, 0, 1, 0, 0, 1, 1, 1])
            vtx = self._shade(p['vtx'])

            def fn(s, t):
                u, v = _uv_at(uv, s, t)
                c = self.sample(mat, u, v, k)
                vc = _bilerp(vtx, s, t)
                return [c[0] * vc[0] / 255, c[1] * vc[1] / 255, c[2] * vc[2] / 255, c[3] * vc[3] / 255 * a]
            self.quad(Mp, rect, fn)
        elif p['type'] == 'wnd':
            self._window(p, Mp, rect, a)
        for kid in p['kids']:
            self._draw(kid, Mp, a)

    def _window(self, p, Mp, rect, a):
        x0, y0, x1, y1 = rect
        inf = p['inflate']

        def fsz(f):
            m = self.mats[f['mat']]
            k = self.primary(m)
            t = self.tex.get(m['maps'][k]['tex']) if k >= 0 else None
            return (t.w, t.h) if t else (0, 0)
        f0 = fsz(p['frames'][0]) if p['frames'] else (0, 0)
        cm = self.mats[p['mat']]
        ck = self.primary(cm)
        uv = p['uv'][0] if p['uv'] else [0, 0, 1, 0, 0, 1, 1, 1]
        vtx = self._shade(p['vtx'])

        def content(s, t):
            u, v = _uv_at(uv, s, t)
            c = self.sample(cm, u, v, ck)
            vc = _bilerp(vtx, s, t)
            return [c[0] * vc[0] / 255, c[1] * vc[1] / 255, c[2] * vc[2] / 255, c[3] * vc[3] / 255 * a]
        self.quad(Mp, [x0 - inf[0] + f0[0], y0 - inf[2] + f0[1], x1 + inf[1] - f0[0], y1 + inf[3] - f0[1]], content)
        frames = p['frames']
        if len(frames) == 1:
            frames = [{'mat': frames[0]['mat'], 'flip': fl} for fl in (0, 1, 2, 4)]
        if len(frames) != 4:
            return
        sz = [fsz(f) if fsz(f)[0] else (8, 8) for f in frames]
        (lw, lh), (rw, rh), (bw, bh), (qw, qh) = sz
        regions = [(x0, y0, x1 - rw, y0 + lh, 0), (x1 - rw, y0, x1, y1 - qh, 1),
                   (x0, y0 + lh, x0 + bw, y1, 2), (x0 + bw, y1 - qh, x1, y1, 3)]
        for a0, b0, a1, b1, i in regions:
            m = self.mats[frames[i]['mat']]
            k = self.primary(m)
            tw, th = sz[i]

            def fn(s, t, a0=a0, b0=b0, a1=a1, b1=b1, i=i, m=m, k=k, tw=tw, th=th):
                px, py = s * (a1 - a0), t * (b1 - b0)
                dx = (a1 - a0) - px if i in (1, 3) else px
                dy = (b1 - b0) - py if i in (2, 3) else py
                dx, dy = min(dx, tw - 0.5), min(dy, th - 0.5)
                c = self.sample(m, dx / tw, dy / th, k)
                return [c[0], c[1], c[2], c[3] * a]
            self.quad(Mp, [a0, b0, a1, b1], fn)


def crop(buf, x0=None, y0=None, x1=None, y1=None):
    """Crops a buffer to the given box, or to what was drawn (alpha > 2)."""
    if x0 is None:
        h, w = len(buf), len(buf[0])
        xs = [x for y in range(h) for x in range(w) if buf[y][x][3] > 2]
        ys = [y for y in range(h) for x in range(w) if buf[y][x][3] > 2]
        x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    return [row[x0:x1] for row in buf[y0:y1]]
