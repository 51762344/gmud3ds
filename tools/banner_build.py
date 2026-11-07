#!/usr/bin/env python3
"""Build the CIA HOME-menu banner assets (256x128 RGBA title plate + chime).

The plate is only the game title 「白金英雄坛说」: the same GNU Unifont 16x16
bitmaps as data/font.bin, set 2x with wide tracking, one extra emboldening
pass, the 原味绿 palette plus a dark outline and drop shadow.  Everything else
stays transparent, so HOME's own background shows through the banner.  The
banner is packed as a plain 2D image (bannertool -i), i.e. it is displayed
flat, without the extended-banner 3D swing.

    python tools/banner_build.py

Output (committed, so the CIA build needs no Python):
    assets/banner/gmud_banner.png   256x128, RGBA, transparent background
    assets/banner/gmud_banner.wav   16-bit stereo PCM chime, 44.1 kHz, <= 3 s

tools/cia/build_cia.sh turns these two into ExeFS:/banner with bannertool.
Only the Python standard library is used.

The WAV format is not free: the HOME menu's banner player wants 44.1 kHz,
16-bit stereo PCM and at most 3 s.  A mono (or lower-rate) file is not
rejected, but it is played as interleaved stereo data: half the duration and
one octave up, which sounds like a burst of rapid beeps instead of the chime.
"""

from __future__ import annotations

import argparse
import gzip
import os
import struct
import sys
import wave
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import icon_art as art

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 256, 128
TITLE = '白金英雄坛说'

ACCENT = '#cfe0a8'      # UI_COL_ACCENT in source/gfx.c
HILITE = '#e8f2c8'
FOOT = '#a8bc74'
EDGE = '#0d1408'
SHADOW = '#22300f'


# ------------------------------------------------------------------ unifont
def load_glyphs(path: str, chars: str):
    wanted = {ord(c) for c in chars}
    out = {}
    opener = gzip.open if path.endswith('.gz') else open
    with opener(path, 'rb') as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith(b'#'):
                continue
            head, _, body = line.partition(b':')
            try:
                cp = int(head, 16)
            except ValueError:
                continue
            if cp not in wanted or not body:
                continue
            hexstr = body.decode('ascii').strip()
            if len(hexstr) != 64:        # 16x16 glyph only
                continue
            rows = [bytes.fromhex(hexstr[y * 4:y * 4 + 4]) for y in range(16)]
            out[cp] = rows
    missing = wanted - set(out)
    if missing:
        raise ValueError('Unifont missing characters: %s' %
                         ''.join(chr(c) for c in sorted(missing)))
    return out


# --------------------------------------------------------------------- art
def text_width(text: str, scale: int, spacing: int) -> int:
    return len(text) * 16 * scale + (len(text) - 1) * spacing


def draw_mask(glyphs, text: str, x: int, y: int, scale: int, spacing: int):
    mask = [[False] * W for _ in range(H)]
    for i, ch in enumerate(text):
        rows = glyphs[ord(ch)]
        gx = x + i * (16 * scale + spacing)
        for yy in range(16):
            for xx in range(16):
                if (rows[yy][xx // 8] >> (7 - (xx & 7))) & 1:
                    for dy in range(scale):
                        for dx in range(scale):
                            mx, my = gx + xx * scale + dx, y + yy * scale + dy
                            if 0 <= mx < W and 0 <= my < H:
                                mask[my][mx] = True
    return mask


def embolden(mask):
    """Grow every stem by one pixel on all four sides (2px -> 4px)."""
    out = [[False] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            if (mask[y][x]
                    or (x > 0 and mask[y][x - 1])
                    or (x + 1 < W and mask[y][x + 1])
                    or (y > 0 and mask[y - 1][x])
                    or (y + 1 < H and mask[y + 1][x])):
                out[y][x] = True
    return out


def paint(cv, mask, top: int, height: int):
    for y in range(H):
        for x in range(W):
            if mask[y][x] and x + 2 < W and y + 2 < H:
                cv.set(x + 2, y + 2, SHADOW)
    for y in range(H):
        for x in range(W):
            if mask[y][x]:
                continue
            edge = False
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < W and 0 <= ny < H and mask[ny][nx]:
                        edge = True
                        break
                if edge:
                    break
            if edge:
                cv.set(x, y, EDGE)
    for y in range(H):
        for x in range(W):
            if not mask[y][x]:
                continue
            if y < top + 3:
                c = HILITE
            elif y >= top + height - 3:
                c = FOOT
            else:
                c = ACCENT
            cv.set(x, y, c)


def build_title_layer(glyphs):
    """纯文字横幅层：只有标题字，其余像素透明。"""
    cv = art.Cv(W, H)
    scale, spacing = 2, 5
    tw = text_width(TITLE, scale, spacing) + 2   # +2 for the embolden pass
    tx = (W - tw) // 2
    ty = (H - 16 * scale) // 2
    mask = embolden(draw_mask(glyphs, TITLE, tx, ty, scale, spacing))
    paint(cv, mask, ty, 16 * scale)
    return cv.p


def write_png_rgba(path: str, w: int, h: int, pixels) -> None:
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            c = pixels[y][x]
            if c is None:
                raw += b'\x00\x00\x00\x00'
            else:
                raw += bytes((int(c[1:3], 16), int(c[3:5], 16),
                              int(c[5:7], 16), 255))

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' +
                chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(bytes(raw), 9)) +
                chunk(b'IEND', b''))


# --------------------------------------------------------------------- wav
def build_wav(path: str):
    """0.72 s 五声音阶方波小提示音，游戏 chiptune 味道。

    Format is dictated by the HOME menu banner player: 44.1 kHz, 16-bit,
    stereo PCM, <= 3 s.  Mono would be read as interleaved stereo (half the
    duration, one octave up), which is heard as rapid beeps.
    """
    rate = 44100
    notes = [(523.25, 0.00, 0.12), (659.25, 0.12, 0.12),
             (783.99, 0.24, 0.12), (1046.50, 0.36, 0.30)]
    total = int(rate * 0.72)
    frames = bytearray()
    for i in range(total):
        t = i / rate
        v = 0.0
        for freq, start, dur in notes:
            if start <= t < start + dur:
                env = min(1.0, (t - start) / 0.008) * min(1.0, (start + dur - t) / 0.05)
                phase = (t - start) * freq
                # 方波 + 一点二次谐波，柔和些
                s = (1.0 if (phase % 1.0) < 0.5 else -1.0) * 0.55
                s += (1.0 if (phase * 2 % 1.0) < 0.5 else -1.0) * 0.12
                v = s * env * 0.34
        pcm = max(-32768, min(32767, int(v * 32767)))
        frames += struct.pack('<hh', pcm, pcm)
    with wave.open(path, 'wb') as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(bytes(frames))
    return os.path.getsize(path)


def main():
    ap = argparse.ArgumentParser(description='build CIA banner assets')
    ap.add_argument('--hex', default=os.path.join(ROOT, 'ref', 'unifont',
                                                  'unifont_all-16.0.01.hex.gz'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'assets', 'banner'))
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    glyphs = load_glyphs(args.hex, TITLE)

    png = os.path.join(args.out, 'gmud_banner.png')
    write_png_rgba(png, W, H, build_title_layer(glyphs))
    print('%-24s %dx%d (%d B)' % ('gmud_banner.png', W, H, os.path.getsize(png)))
    wav = os.path.join(args.out, 'gmud_banner.wav')
    print('%-24s %d B' % ('gmud_banner.wav', build_wav(wav)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
