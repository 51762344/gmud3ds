#!/usr/bin/env python3
"""Build the 3DS HOME-menu icon PNGs (and optionally verify an SMDH).

Artwork lives in tools/icon_art.py: the original 32x48 hero sprite from
ref/gmud/src/player.s, 1:1, recoloured only with the game's own "原味绿"
four-shade palette.  The sprite itself is original game art; see
THIRD-PARTY-NOTICES.md.

Output : icon.png        48x48 large icon  (whole hero, LCD frame)
         icon_small.png  24x24 small icon  (1:1 head close-up; HOME menu grid)

Usage:
    python tools/icon_build.py                       # write the two PNGs
    python tools/icon_build.py --preview p.png       # plus a zoomed preview
    python tools/icon_build.py --verify gmud3ds.smdh
    python tools/icon_build.py --compare            # check Pixel Loom exports

Only the Python standard library is used.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import icon_art as art

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LARGE_SIZE, SMALL_SIZE = 48, 24
SMDH_SMALL_OFF, SMDH_LARGE_OFF = 0x2040, 0x24C0


# ----------------------------------------------------------------------- png
def write_png(path: str, w: int, h: int, pixels) -> None:
    """8-bit RGB PNG, no alpha (SMDH icons are RGB565)."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            c = pixels[y][x]
            raw += bytes((int(c[1:3], 16), int(c[3:5], 16), int(c[5:7], 16)))

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b'\x89PNG\r\n\x1a\n' +
           chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(bytes(raw), 9)) +
           chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)


def read_png(path: str):
    """Minimal 8-bit RGB/RGBA PNG decoder (filters 0-4)."""
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG: %s' % path
    pos, idat, w = 8, bytearray(), 0
    ctype = h = None
    while pos < len(data):
        n = struct.unpack('>I', data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if tag == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            assert depth == 8 and ctype in (2, 6), (depth, ctype)
        elif tag == b'IDAT':
            idat += body
        elif tag == b'IEND':
            break
        pos += 12 + n
    bpp = 3 if ctype == 2 else 4
    stride = w * bpp
    raw = zlib.decompress(bytes(idat))
    rows, prev, off = [], bytearray(stride), 0
    for _ in range(h):
        ft = raw[off]
        line = bytearray(raw[off + 1:off + 1 + stride])
        off += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1: line[i] = (line[i] + a) & 255
            elif ft == 2: line[i] = (line[i] + b) & 255
            elif ft == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append([('#%02x%02x%02x' % (line[x * bpp], line[x * bpp + 1], line[x * bpp + 2]))
                     for x in range(w)])
        prev = line
    return w, h, rows


# ------------------------------------------------------------------- previews
def write_preview(path: str, large, small) -> None:
    z1, z2, gap = 5, 10, 12
    w = LARGE_SIZE * z1 + gap + SMALL_SIZE * z2 + 16
    h = LARGE_SIZE * z1 + 16
    canvas = [[art.S4] * w for _ in range(h)]
    for y in range(LARGE_SIZE * z1):
        for x in range(LARGE_SIZE * z1):
            canvas[8 + y][8 + x] = large[y // z1][x // z1]
    for y in range(SMALL_SIZE * z2):
        for x in range(SMALL_SIZE * z2):
            canvas[8 + y][8 + LARGE_SIZE * z1 + gap + x] = small[y // z2][x // z2]
    write_png(path, w, h, canvas)


# --------------------------------------------------------------------- verify
def _zorder_8x8():
    order = []
    for c in range(64):
        x = (c & 1) | ((c >> 1) & 2) | ((c >> 2) & 4) | ((c >> 3) & 8)
        y = ((c >> 1) & 1) | ((c >> 2) & 2) | ((c >> 3) & 4)
        order.append((x, y))
    return order


ZORDER = _zorder_8x8()


def decode_smdh_icon(data: bytes, off: int, n: int):
    """SMDH icons are 8x8 tiled textures, Z-order inside a tile."""
    px = [[0] * n for _ in range(n)]
    idx = 0
    for ty in range(n // 8):
        for tx in range(n // 8):
            for dx, dy in ZORDER:
                v = struct.unpack('<H', data[off + idx * 2: off + idx * 2 + 2])[0]
                px[ty * 8 + dy][tx * 8 + dx] = v
                idx += 1
    return px


def to_rgb565(color: str) -> int:
    r, g, b = (int(color[i:i + 2], 16) for i in (1, 3, 5))
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def verify_smdh(path: str, large, small) -> bool:
    data = open(path, 'rb').read()
    ok = True
    for label, off, n, want in (('small', SMDH_SMALL_OFF, SMALL_SIZE, small),
                                ('large', SMDH_LARGE_OFF, LARGE_SIZE, large)):
        got = decode_smdh_icon(data, off, n)
        diff = sum(got[y][x] != to_rgb565(want[y][x])
                   for y in range(n) for x in range(n))
        if diff:
            ok = False
            print('%s icon: %d/%d pixels differ' % (label, diff, n * n))
        else:
            print('%s icon: %dx%d ok' % (label, n, n))
    return ok


# ------------------------------------------------------------------------ main
def main() -> int:
    ap = argparse.ArgumentParser(description='build the HOME menu icon')
    ap.add_argument('--src', default=os.path.join(ROOT, 'ref', 'gmud', 'src'))
    ap.add_argument('--out', default=ROOT)
    ap.add_argument('--preview', help='also write a zoomed preview PNG')
    ap.add_argument('--verify', help='verify the icons inside this SMDH')
    args = ap.parse_args()

    states = art.frame_states(art.parse_player_s(args.src))
    large = art.flatten(art.big_layers(states))
    small = art.flatten(art.small_layers(states))

    if not args.verify:
        os.makedirs(args.out, exist_ok=True)
        for name, size, img in (('icon.png', LARGE_SIZE, large),
                                ('icon_small.png', SMALL_SIZE, small)):
            p = os.path.join(args.out, name)
            write_png(p, size, size, img)
            print('%-16s %dx%d (%d B)' % (name, size, size, os.path.getsize(p)))

    if args.preview:
        write_preview(args.preview, large, small)
        print('preview         %s' % args.preview)

    bad = False
    if args.verify:
        bad |= not verify_smdh(args.verify, large, small)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
