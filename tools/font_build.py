#!/usr/bin/env python3
"""Build data/font.bin (1bpp glyph atlas pages) from GNU Unifont .hex.

Input : fixed Unifont release, e.g. ref/unifont/unifont_all-16.0.01.hex(.gz)
Output: data/font.bin  custom "GFNT" v1 pack
        data/font_preview.png (debug aid, optional)

Charset = ASCII 0x20-0x7E
        + GB2312 symbol rows 0xA1-0xA9 and level-1 hanzi rows 0xB0-0xD7
        + every non-ASCII codepoint found in the scanned text files
          (source/*.c and data/*.json by default).

Only the Python standard library is used.
"""

from __future__ import annotations

import argparse
import glob
import gzip
import os
import struct
import sys
import zlib

MAGIC = b"GFNT"
VERSION = 1
CELL_W = 16
CELL_H = 16
HEADER_SIZE = 28
INDEX_SIZE = 14
DEFAULT_ATLAS = 1024

# Same theme as the UI ("原味绿").
PAPER = (0xAE, 0xC2, 0x7E)
INK = (0x22, 0x30, 0x0F)


def sha256_note(path: str) -> str:
    import hashlib

    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


# ------------------------------------------------------------------ charset


def gb2312_charset() -> set[int]:
    chars: set[int] = set()
    rows = list(range(0xA1, 0xAA)) + list(range(0xB0, 0xD8))
    for hi in rows:
        for lo in range(0xA1, 0xFF):
            try:
                s = bytes((hi, lo)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if len(s) == 1:
                chars.add(ord(s))
    return chars


def scan_text_chars(paths: list[str]) -> set[int]:
    chars: set[int] = set()
    for pattern in paths:
        for path in glob.glob(pattern, recursive=True):
            if os.path.isdir(path):
                continue
            try:
                with open(path, "r", encoding="utf-8") as f:
                    text = f.read()
            except (UnicodeDecodeError, OSError):
                continue
            for ch in text:
                if ord(ch) >= 0x80:
                    chars.add(ord(ch))
    return chars


# ------------------------------------------------------------------ unifont


def open_hex(path: str):
    if path.endswith(".gz"):
        return gzip.open(path, "rb")
    return open(path, "rb")


def parse_hex_glyph(hexstr: str):
    """Return (width, rows) where rows are bytes, or None if unsupported."""
    n = len(hexstr)
    if n == 32:
        width = 8
    elif n == 64:
        width = 16
    else:
        return None
    height = 16
    stride = width // 8
    rows = []
    for y in range(height):
        chunk = hexstr[y * stride * 2:(y + 1) * stride * 2]
        rows.append(bytes.fromhex(chunk))
    return width, height, rows


def load_unifont(path: str, wanted: set[int]):
    glyphs: dict[int, tuple[int, int, list[bytes]]] = {}
    missing = set(wanted)
    with open_hex(path) as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith(b"#"):
                continue
            if b"#" in line:
                line = line.split(b"#", 1)[0].strip()
            head, _, body = line.partition(b":")
            if not body:
                continue
            try:
                cp = int(head, 16)
            except ValueError:
                continue
            if cp not in missing:
                continue
            parsed = parse_hex_glyph(body.decode("ascii"))
            missing.discard(cp)
            if parsed is None:
                continue
            glyphs[cp] = parsed
    return glyphs, missing


# --------------------------------------------------------------------- pack


def pack_font(glyphs: dict[int, tuple[int, int, list[bytes]]], atlas: int):
    cell_cols = atlas // CELL_W
    cell_rows = atlas // CELL_H
    per_page = cell_cols * cell_rows

    cps = sorted(glyphs)
    page_count = max(1, (len(cps) + per_page - 1) // per_page)
    pages = [bytearray(atlas * atlas // 8) for _ in range(page_count)]
    index = []

    for order, cp in enumerate(cps):
        width, height, rows = glyphs[cp]
        page = order // per_page
        slot = order % per_page
        cx = (slot % cell_cols) * CELL_W
        cy = (slot // cell_cols) * CELL_H
        advance = 8 if width == 8 else 16
        index.append(struct.pack("<IHHHBBBB", cp, cx, cy, page, width, height, advance, 0))

        page_buf = pages[page]
        for y in range(height):
            row = rows[y]
            for bx in range(width // 8):
                byte = row[bx]
                for bit in range(8):
                    if byte & (0x80 >> bit):
                        x = cx + bx * 8 + bit
                        gy = cy + y
                        off = gy * atlas // 8 + x // 8
                        page_buf[off] |= 0x80 >> (x & 7)

    return cps, page_count, pages, b"".join(index)


def write_font_bin(path: str, atlas: int, cps, page_count, pages, index: bytes) -> None:
    header = bytearray(HEADER_SIZE)
    header[0:4] = MAGIC
    struct.pack_into("<HHHHIIII", header, 4, VERSION, CELL_W, CELL_H, page_count,
                     len(cps), atlas, atlas, atlas * atlas // 8)
    with open(path, "wb") as f:
        f.write(header)
        f.write(index)
        for page in pages:
            f.write(page)


# ------------------------------------------------------------------- preview


def write_png(path: str, width: int, height: int, pixels: bytearray) -> None:
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(pixels[y * width * 3:(y + 1) * width * 3])

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def render_preview(path: str, glyphs, lines: list[str], scale: int = 2) -> None:
    pad = 8
    line_h = CELL_H * scale
    width = pad * 2 + max(sum(8 if ord(c) < 0x80 else 16 for c in line) for line in lines) * scale
    height = pad * 2 + line_h * len(lines)
    bg = bytes(PAPER)
    pixels = bytearray(bg * (width * height))

    def put(x: int, y: int):
        if 0 <= x < width and 0 <= y < height:
            off = (y * width + x) * 3
            pixels[off:off + 3] = bytes(INK)

    for li, line in enumerate(lines):
        x = pad
        y = pad + li * line_h
        for ch in line:
            cp = ord(ch)
            g = glyphs.get(cp)
            if g is None:
                x += 8 if cp < 0x80 else 16
                continue
            gw, gh, rows = g
            advance = 8 if gw == 8 else 16
            for gy in range(gh):
                for gx in range(gw):
                    if rows[gy][gx // 8] & (0x80 >> (gx & 7)):
                        for sy in range(scale):
                            for sx in range(scale):
                                put(x + gx * scale + sx, y + gy * scale + sy)
            x += advance * scale

    write_png(path, width, height, pixels)


# ---------------------------------------------------------------------- main


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--hex", required=True, help="Unifont .hex or .hex.gz")
    ap.add_argument("--out", default="data/font.bin")
    ap.add_argument("--scan", action="append", default=None,
                    help="UTF-8 files/globs whose non-ASCII chars must be covered "
                         "(default: source/*.c, data/maps.json)")
    ap.add_argument("--no-gb2312", action="store_true", help="skip GB2312 seed charset")
    ap.add_argument("--atlas", type=int, default=DEFAULT_ATLAS, help="square page size")
    ap.add_argument("--preview", default=None, help="write a sample PNG here")
    ap.add_argument("--allow-missing", action="store_true",
                    help="warn instead of failing when Unifont lacks a codepoint")
    args = ap.parse_args()

    if args.atlas % CELL_W or args.atlas & (args.atlas - 1):
        print("error: --atlas must be a power-of-two multiple of 16", file=sys.stderr)
        return 2

    scans = args.scan
    if scans is None:
        scans = ["source/*.c", "data/maps.json", "data/items.json",
                 "data/npcs.json", "data/skills.json",
                 "data/fighttext_chars.txt"]

    wanted: set[int] = set(range(0x20, 0x7F))
    if not args.no_gb2312:
        wanted |= gb2312_charset()
    wanted |= scan_text_chars(scans)
    wanted.discard(0)

    print("charset: %d codepoints (%s)"
          % (len(wanted), "GB2312 symbols + level-1" if not args.no_gb2312 else "ASCII only"))
    glyphs, missing = load_unifont(args.hex, wanted)
    if missing:
        sample = "".join(chr(c) for c in sorted(missing)[:24])
        print("error: %d codepoints missing from %s: %s%s"
              % (len(missing), args.hex, sample,
                 " ..." if len(missing) > 24 else ""), file=sys.stderr)
        if not args.allow_missing:
            return 1

    cps, page_count, pages, index = pack_font(glyphs, args.atlas)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    write_font_bin(args.out, args.atlas, cps, page_count, pages, index)

    per_page = (args.atlas // CELL_W) * (args.atlas // CELL_H)
    total = os.path.getsize(args.out)
    print("glyphs=%d pages=%d (%d/page) cell=%dx%d atlas=%dx%d"
          % (len(cps), page_count, per_page, CELL_W, CELL_H, args.atlas, args.atlas))
    print("%s %d B (header %d + index %d + bitmap %d)"
          % (args.out, total, HEADER_SIZE, len(index),
             sum(len(p) for p in pages)))
    print("sha256 %s" % sha256_note(args.out))
    if page_count > 1:
        print("note: glyphs span %d A8 texture pages; runtime uploads all of them" % page_count)

    if args.preview:
        lines = [
            "白金英雄坛说 平安镇东 商业街 豆腐店",
            "这位客官也是来吃豆腐的么？我的豆腐又白又嫩。",
            "体力100/100 内力60/60 文钱100 辰时",
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789",
        ]
        render_preview(args.preview, glyphs, lines)
        print("preview %s" % args.preview)

    return 0


if __name__ == "__main__":
    sys.exit(main())
