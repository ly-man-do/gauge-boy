"""3DS file formats for the system UI pipeline (make_sysui.py). Standard library only.

- LZ11 decompression (the HOME Menu's and System Settings' *_LZ.bin archives)
- DARC archives
- BCLIM textures (L8, A8, LA4, LA8, L4, A4, RGB565, RGBA4, RGBA5551, RGBA8; stored
  padded to power-of-two sizes in 8x8 Morton-ordered tiles)
- writing RGBA PNGs
"""
import struct
import zlib

ALPHA_ONLY = {1, 13}   # A8, A4: the GPU reads their color as black


def lz11(d):
    """Decompresses an LZ11 (type 0x11) buffer."""
    assert d[0] == 0x11, 'not LZ11'
    size = d[1] | d[2] << 8 | d[3] << 16
    p = 4
    if size == 0:
        size = struct.unpack_from('<I', d, 4)[0]
        p = 8
    out = bytearray()
    while len(out) < size:
        flags = d[p]
        p += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                b = d[p]
                ind = b >> 4
                if ind == 0:
                    n = ((b & 0xF) << 4 | d[p + 1] >> 4) + 0x11
                    disp = ((d[p + 1] & 0xF) << 8 | d[p + 2]) + 1
                    p += 3
                elif ind == 1:
                    n = ((b & 0xF) << 12 | d[p + 1] << 4 | d[p + 2] >> 4) + 0x111
                    disp = ((d[p + 2] & 0xF) << 8 | d[p + 3]) + 1
                    p += 4
                else:
                    n = ind + 1
                    disp = ((b & 0xF) << 8 | d[p + 1]) + 1
                    p += 2
                for _ in range(n):
                    out.append(out[-disp])
            else:
                out.append(d[p])
                p += 1
    return bytes(out)


def darc(d):
    """Returns {path: bytes} for every file in a DARC archive."""
    assert d[:4] == b'darc', 'not DARC'
    tab_off = struct.unpack_from('<I', d, 0x10)[0]
    count = struct.unpack_from('<I', d, tab_off + 8)[0]
    names_off = tab_off + count * 12

    def name_at(o):
        s = bytearray()
        i = names_off + o
        while d[i:i + 2] != b'\0\0':
            s += d[i:i + 2]
            i += 2
        return s.decode('utf-16-le')

    files, stack = {}, [(count, '')]
    for i in range(1, count):
        while stack and i >= stack[-1][0]:
            stack.pop()
        no, a, b = struct.unpack_from('<III', d, tab_off + i * 12)
        name = name_at(no & 0xFFFFFF)
        base = stack[-1][1] if stack else ''
        path = (base + '/' + name).lstrip('/')
        if no >> 24 & 1:
            stack.append((b, path))
        else:
            files[path] = d[a:a + b]
    return files


def archive(data):
    """An *_LZ.bin file's contents as {path: bytes}."""
    if data[0] == 0x11:
        data = lz11(data)
    return darc(data)


_BPP = {0: 8, 1: 8, 2: 8, 3: 16, 5: 16, 6: 24, 7: 16, 8: 16, 9: 32, 12: 4, 13: 4}


def _pixel(fmt, d, i):
    if fmt in (12, 13):
        b = d[i // 2]
        v = ((b >> 4) if i & 1 else (b & 0xF)) * 17
        return (255, 255, 255, v) if fmt == 13 else (v, v, v, 255)
    if fmt == 0:
        return (d[i], d[i], d[i], 255)
    if fmt == 1:
        return (255, 255, 255, d[i])
    if fmt == 2:
        b = d[i]
        lum = (b >> 4) * 17
        return (lum, lum, lum, (b & 0xF) * 17)
    if fmt == 3:
        a, lum = d[2 * i], d[2 * i + 1]
        return (lum, lum, lum, a)
    if fmt == 5:
        v = struct.unpack_from('<H', d, 2 * i)[0]
        return ((v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, 255)
    if fmt == 7:
        v = struct.unpack_from('<H', d, 2 * i)[0]
        return ((v >> 11) * 255 // 31, (v >> 6 & 31) * 255 // 31, (v >> 1 & 31) * 255 // 31,
                255 if v & 1 else 0)
    if fmt == 8:
        v = struct.unpack_from('<H', d, 2 * i)[0]
        return ((v >> 12) * 17, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17)
    if fmt == 9:
        a, b, g, r = d[4 * i:4 * i + 4]
        return (r, g, b, a)
    raise ValueError('BCLIM format %d not supported' % fmt)


class Texture:
    """A decoded BCLIM: w, h, fmt and rows of (r, g, b, a)."""

    def __init__(self, data):
        i = data.rfind(b'imag')
        self.w, self.h, self.fmt = struct.unpack_from('<HHI', data, i + 8)
        p2 = lambda v: max(8, 1 << (v - 1).bit_length())
        tw, th = p2(self.w), p2(self.h)          # stored padded to powers of two
        if tw * th * _BPP[self.fmt] // 8 > len(data) - 0x28:
            tw, th = (self.w + 7) // 8 * 8, (self.h + 7) // 8 * 8
        self.rows = [[(0, 0, 0, 0)] * self.w for _ in range(self.h)]
        k = 0
        for ty in range(0, th, 8):
            for tx in range(0, tw, 8):
                for n in range(64):
                    x = sum(((n >> (2 * b)) & 1) << b for b in range(3))
                    y = sum(((n >> (2 * b + 1)) & 1) << b for b in range(3))
                    if ty + y < self.h and tx + x < self.w:
                        self.rows[ty + y][tx + x] = _pixel(self.fmt, data, k)
                    k += 1

    @property
    def alpha_only(self):
        return self.fmt in ALPHA_ONLY


def write_png(path, rows):
    """Writes rows of (r, g, b, a) (0-255, may be floats) as an RGBA PNG."""
    h, w = len(rows), len(rows[0])
    raw = b''.join(b'\0' + bytes(max(0, min(255, int(round(c)))) for p in row for c in p)
                   for row in rows)

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
