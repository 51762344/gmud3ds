#!/usr/bin/env python3
"""Extract GMUD map/tile resources into the engine's runtime formats.

Input : ref/gmud/src/data/map/gmud.map + ref/gmud/src/data/img/gmud_img.bin
        ref/gmud/src/goods.dat + ref/gmud/src/data/npc_data + ref/gmud/src/h/id.h
Output: data/maps.bin  (custom v1 pack)
        data/maps.json (debug dump)
        data/items.bin + data/items.json  (82 items, M3)
        data/npcs.bin + data/npcs.json    (124 NPCs, v2 with combat stats, M4)
        data/skills.bin + data/skills.json (42 kungfu + 24 performs, M4)
        data/player.bin + data/player.png (12 walk frames + 12 masks, 32x48 1bpp)
        data/npcspr.bin (128 npcId-indexed 16x16 auxiliary images, NSPR v1)
        data/fighttext.bin (FTXT v5: limbs, attack/dodge templates, kungfu
                            move descriptions, string2.s damage/status tables
                            and thresholds, the full perform.txt label table)
        data/tiles.bin (verbatim copy, 131072 B)
        data/tiles.png (16x32 grid of opaque ground tiles)
        data/tiles_obj.png (same grid, AND-mask transparency applied)
        data/preview/mapNN.png (ground+object composition, debug aid)

Only the Python standard library is used.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
import zlib

MAP_COUNT = 47
DOOR_COUNT = 111
NPC_COUNT = 138
CELL_TOTAL = 8374

ITEM_COUNT = 82
ITEM_REC_SIZE = 32
# npc_data.gb carries 124 town NPCs + the three final bosses. Id 125 is the
# original runtime-only killer/ghost, so a placeholder keeps boss ids aligned
# with h/id.h (KILLER_NPC=125, BOSS1..3=126..128).
NPC_RECORD_COUNT = 128
NPC_REC_SIZE = 376          # v6: desc 字段 112 -> 256 B（整段描述不再被截断）
NPC_DESC_LEN = 256
NPC_MAX_KF = 20
NPC_MAX_USEKF = 5
NPC_VENDOR_MAX = 10

KF_COUNT = 42
PF_COUNT = 24
SKILL_REC_SIZE = 24

TILE_COUNT = 512
TILE_BYTES = 128
IMAGE_BYTES = TILE_COUNT * TILE_BYTES          # 65536
TILES_BIN_BYTES = IMAGE_BYTES * 2              # 131072

SHA256_MAP = "311b4331489be637d23a11ee1b36383b6b94d082ed59ea7bb5809b42afacc79a"
SHA256_IMG = "bc18995c8072ec7c2635dac9ab8faead504b3661671cfb3082a8ea4d0c42ac24"

# Same theme as the UI ("原味绿").
SHADE = [(0xAE, 0xC2, 0x7E), (0x8F, 0xA9, 0x68), (0x5C, 0x7A, 0x3E), (0x22, 0x30, 0x0F)]
PAPER = SHADE[0]
INK = SHADE[3]

NAME_LEN = 24
MAP_ENTRY_SIZE = 44
HEADER_SIZE = 32


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


# --------------------------------------------------------------------------- png


def write_png(path: str, width: int, height: int, rows: list[bytes], alpha: bool = False) -> None:
    """Write an 8-bit RGB/RGBA PNG from raw rows (color type 2 or 6)."""
    assert len(rows) == height
    stride = width * (4 if alpha else 3)
    for row in rows:
        assert len(row) == stride, (len(row), stride)

    raw = bytearray()
    for row in rows:
        raw.append(0)          # filter type 0
        raw.extend(row)

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6 if alpha else 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")

    with open(path, "wb") as f:
        f.write(png)


# ------------------------------------------------------------------- map parsing


class Map:
    __slots__ = ("index", "w", "h", "ground", "name", "cells")

    def __init__(self, index: int, w: int, h: int, ground, name: str, cells):
        self.index = index
        self.w = w
        self.h = h
        self.ground = ground
        self.name = name
        self.cells = cells


def parse_world(path: str):
    data = open(path, "rb").read()
    if len(data) < 11:
        raise ValueError("gmud.map too small")

    n_map, n_door = struct.unpack_from("<HH", data, 0)
    start = (data[4], data[5], data[6])
    door_off, npc_off = struct.unpack_from("<HH", data, 7)
    addr = struct.unpack_from("<%dH" % n_map, data, 11)

    if 11 + n_map * 2 != door_off:
        raise ValueError("door table does not follow the map offset table")
    if door_off + n_door * 6 != npc_off:
        raise ValueError("npc table does not follow the door table")

    # NPC count is implicit: the first map record ends the NPC table.
    if (addr[0] - npc_off) % 3:
        raise ValueError("NPC table size is not a multiple of 3")
    n_npc = (addr[0] - npc_off) // 3

    maps = []
    for i, off in enumerate(addr):
        if off + 34 > len(data):
            raise ValueError("map %d record offset out of range" % i)
        w, h = data[off], data[off + 1]
        ground = struct.unpack_from("<8H", data, off + 2)
        raw_name = data[off + 18:off + 34].split(b"\x00", 1)[0]
        try:
            name = raw_name.decode("gb2312")
        except UnicodeDecodeError as exc:
            raise ValueError("map %d name is not GB2312: %r" % (i, raw_name)) from exc
        cells = struct.unpack_from("<%dH" % (w * h), data, off + 34)
        maps.append(Map(i, w, h, ground, name, cells))

        end = off + 34 + w * h * 2
        if i + 1 < n_map:
            if end != addr[i + 1]:
                raise ValueError("map %d record size mismatch (end=%#x next=%#x)" % (i, end, addr[i + 1]))
        elif end != len(data):
            raise ValueError("last map ends at %#x, file is %#x" % (end, len(data)))

    doors = [
        {"index": i, "tile": t, "targetMap": m, "pad": pad, "tx": x, "ty": y}
        for i, (t, m, pad, x, y) in enumerate(
            struct.unpack_from("<HBBBB", data, door_off + i * 6) for i in range(n_door)
        )
    ]
    npcs = [
        {"index": i, "tile": t, "npcId": n}
        for i, (t, n) in enumerate(
            struct.unpack_from("<HB", data, npc_off + i * 3) for i in range(n_npc)
        )
    ]

    return dict(raw=data, start=start, maps=maps, doors=doors, npcs=npcs)


def validate(world) -> dict:
    maps = world["maps"]
    doors = world["doors"]
    npcs = world["npcs"]
    n_map = len(maps)

    assert n_map == MAP_COUNT, n_map
    assert len(doors) == DOOR_COUNT, len(doors)
    assert len(npcs) == NPC_COUNT, len(npcs)
    assert sum(m.w * m.h for m in maps) == CELL_TOTAL
    assert (maps[0].w, maps[0].h, maps[0].name) == (40, 40, "失落的世界")
    assert (maps[3].w, maps[3].h, maps[3].name) == (20, 12, "平安镇东")
    assert (maps[38].w, maps[38].h, maps[38].name) == (30, 30, "玉女峰")
    assert maps[46].name == "时空的尽头", maps[46].name

    start_map, start_x, start_y = world["start"]
    assert 0 <= start_map < n_map, start_map
    assert start_x < maps[start_map].w and start_y < maps[start_map].h

    for m in maps:
        assert 1 <= m.w <= 255 and 1 <= m.h <= 255, (m.index, m.w, m.h)
        assert len(m.name.encode("utf-8")) <= NAME_LEN, (m.index, m.name)
        for t in m.ground:
            assert t < TILE_COUNT, (m.index, t)
        for cell in m.cells:
            obj = cell & 0x1FFF
            ground = cell >> 13
            assert ground < 8, (m.index, cell)
            if obj < 1024:
                assert obj < TILE_COUNT, (m.index, obj)
            elif obj < 2048:
                assert obj - 1024 < TILE_COUNT, (m.index, obj)
            elif obj < 3072:
                assert obj - 2048 < len(doors), (m.index, obj)
            else:
                assert obj - 3072 < len(npcs), (m.index, obj)

    ref_doors = set()
    ref_npcs = set()
    for m in maps:
        for cell in m.cells:
            obj = cell & 0x1FFF
            if 2048 <= obj < 3072:
                ref_doors.add(obj - 2048)
            elif obj >= 3072:
                ref_npcs.add(obj - 3072)

    for d in doors:
        d["referenced"] = d["index"] in ref_doors
        d["valid"] = d["targetMap"] < n_map
        if d["referenced"]:
            assert d["valid"], d
            tm = maps[d["targetMap"]]
            assert d["tx"] < tm.w and d["ty"] < tm.h, d
            assert d["tile"] < TILE_COUNT, d
        elif d["valid"]:
            tm = maps[d["targetMap"]]
            d["valid"] = d["tx"] < tm.w and d["ty"] < tm.h

    for n in npcs:
        n["referenced"] = n["index"] in ref_npcs
        if n["referenced"]:
            assert n["tile"] < TILE_COUNT, n
    # NPC table entries that no cell points at are allowed to be placeholders,
    # but every referenced index must be inside the table (checked above).

    # Referenced object ids must resolve to a tile inside the atlas.
    for m in maps:
        for cell in m.cells:
            obj = cell & 0x1FFF
            if obj < 1024:
                tile = obj
            elif obj < 2048:
                tile = obj - 1024
            elif obj < 3072:
                tile = doors[obj - 2048]["tile"]
            else:
                tile = npcs[obj - 3072]["tile"]
            assert tile < TILE_COUNT, (m.index, obj, tile)

    unused_doors = [d["index"] for d in doors if not d["referenced"]]
    return {
        "unusedDoors": unused_doors,
        "referencedDoors": len(ref_doors),
        "referencedNpcs": len(ref_npcs),
    }


# ----------------------------------------------------------------- tiles / png


def tile_pixels(data: bytes, idx: int, masked: bool):
    """Return 32 rows x 32 values: 1=ink, 0=paper, None=transparent."""
    assert 0 <= idx < TILE_COUNT and len(data) == TILES_BIN_BYTES
    out = []
    for y in range(32):
        row = []
        for x in range(32):
            off = idx * TILE_BYTES + y * 4 + x // 8
            shift = 7 - x % 8
            ink = (data[off] >> shift) & 1
            keep = (data[IMAGE_BYTES + off] >> shift) & 1
            row.append(1 if ink else (None if masked and keep else 0))
        out.append(row)
    return out


def tile_rgba(data: bytes, idx: int, masked: bool) -> list[bytes]:
    rows = []
    for row in tile_pixels(data, idx, masked):
        buf = bytearray()
        for v in row:
            if v is None:
                buf.extend((0, 0, 0, 0))
            elif v:
                buf.extend((INK[0], INK[1], INK[2], 255))
            else:
                buf.extend((PAPER[0], PAPER[1], PAPER[2], 255))
        rows.append(bytes(buf))
    return rows


def write_tile_sheet(path: str, data: bytes, masked: bool, alpha: bool) -> None:
    cols, rows_n = 16, 32
    sheet_w, sheet_h = cols * 32, rows_n * 32
    canvas = [bytearray([0] * (sheet_w * 4)) for _ in range(sheet_h)]
    for idx in range(TILE_COUNT):
        ox, oy = (idx % cols) * 32, (idx // cols) * 32
        for y, row in enumerate(tile_rgba(data, idx, masked)):
            canvas[oy + y][ox * 4:(ox + 32) * 4] = row
    rows = [bytes(r) for r in canvas]
    if not alpha:
        rows = [bytes(b for i, b in enumerate(r) if i % 4 != 3) for r in rows]
    write_png(path, sheet_w, sheet_h, rows, alpha=alpha)


def resolve_obj_tile(obj: int, doors, npcs):
    if obj == 0:
        return None
    if obj < 1024:
        return obj
    if obj < 2048:
        tile = obj - 1024
    elif obj < 3072:
        idx = obj - 2048
        tile = doors[idx]["tile"] if idx < len(doors) else None
    else:
        idx = obj - 3072
        tile = npcs[idx]["tile"] if idx < len(npcs) else None
    # tools.s make_ground: tile 0 means "no object image". Keep the ground
    # tile (walls and door openings are drawn by their background instead of
    # being flattened into a blank paper square).
    return tile or None


def compose_map(m: Map, tiles: bytes, doors, npcs) -> list[bytes]:
    """Render one map to RGB rows at 32 px per cell (paper background)."""
    w_px, h_px = m.w * 32, m.h * 32
    canvas = [bytearray([PAPER[0], PAPER[1], PAPER[2]] * w_px) for _ in range(h_px)]
    for by in range(m.h):
        for bx in range(m.w):
            cell = m.cells[by * m.w + bx]
            gtiles = tile_rgba(tiles, m.ground[cell >> 13], False)
            otile = resolve_obj_tile(cell & 0x1FFF, doors, npcs)
            otiles = tile_rgba(tiles, otile, True) if otile is not None else None
            for y in range(32):
                g = gtiles[y]
                target = canvas[by * 32 + y]
                if otiles is None:
                    for x in range(32):
                        target[bx * 32 * 3 + x * 3:bx * 32 * 3 + x * 3 + 3] = g[x * 4:x * 4 + 3]
                else:
                    o = otiles[y]
                    for x in range(32):
                        if o[x * 4 + 3]:
                            px = o[x * 4:x * 4 + 3]
                        else:
                            px = g[x * 4:x * 4 + 3]
                        target[bx * 32 * 3 + x * 3:bx * 32 * 3 + x * 3 + 3] = px
    return [bytes(r) for r in canvas]


# ------------------------------------------------------------- items / npcs


def read_utf8(path: str) -> str:
    return open(path, encoding="utf-8").read()


def split_asm_values(text: str) -> list[str]:
    """Split one db/dw argument list, keeping quoted strings intact."""
    out: list[str] = []
    cur = ""
    quoted = False
    for ch in text:
        if quoted:
            cur += ch
            if ch == "'":
                quoted = False
        elif ch == "'":
            quoted = True
            cur += ch
        elif ch == ",":
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def asm_int(token: str, symbols: dict[str, int]) -> int:
    token = token.strip()
    m = re.match(r"^(\w+)\s*\|\s*80h$", token, re.I)
    if m:
        name = m.group(1)
        if name in symbols:
            return symbols[name] | 0x80
        # Skill ids are not needed by M3; keep the enabled bit only.
        if name.endswith("_KF"):
            return 0
        raise ValueError("unknown assembler symbol: %r" % token)
    if token in symbols:
        return symbols[token]
    if re.fullmatch(r"[0-9][0-9a-fA-F]*h", token):
        return int(token[:-1], 16)
    if re.fullmatch(r"-?(0x[0-9a-fA-F]+|\d+)", token):
        return int(token, 0)
    # Book records hold skill entry points, not item data; M3 only needs the
    # book flag, so collapse those pointers to 0.
    if re.fullmatch(r"book\d+_skill", token) or token.endswith("_KF"):
        return 0
    # NPC skill tables reference more ids than M3 reads.
    if re.fullmatch(r"[A-Z0-9_]+_KF", token):
        return 0
    raise ValueError("unknown assembler symbol: %r" % token)


def id_section(text: str, start: str, end: str) -> list[str]:
    seg = text.split(start, 1)[1].split(end, 1)[0]
    return re.findall(r"define\s+1,\s*(\w+)", seg)


def id_enum(text: str, start: str, end: str) -> list[str]:
    """Parses a `__base equ 0` / `define 1,NAME` enum, honouring resets."""
    seg = text.split(start, 1)[1].split(end, 1)[0]
    names: list[str] = []
    base = 0
    for line in seg.splitlines():
        line = line.split(";", 1)[0].strip()
        if not line:
            continue
        m = re.match(r"__base\s+equ\s+0$", line, re.I)
        if m:
            base = 0
            continue
        m = re.match(r"define\s+1\s*,\s*(\w+)$", line, re.I)
        if m:
            names.append(m.group(1))
            base += 1
    return names


def parse_skill_tables(src_dir: str, idh: str) -> tuple[list[dict], list[dict]]:
    text = read_utf8(os.path.join(src_dir, "skill.dat"))

    kf_names = id_enum(idh, ";kf_id", ";pf_id")
    pf_names = id_enum(idh, ";pf_id", ";pai:")

    kf_symbols = {name: i for i, name in enumerate(
        id_enum(idh, ";kf_type", ";kf_id"))}
    kf_symbols.update({name: i for i, name in enumerate(
        id_enum(idh, ";weapon_type", ";armor_type"))})
    kf_symbols.update({name: i for i, name in enumerate(kf_names)})

    body = text.split("kf_name_tbl:", 1)[1]
    kf_name_entries = re.findall(r"db\s+'([^']*)'\s*,\s*0ffh",
                                 body.split("else", 1)[0])
    if len(kf_name_entries) != len(kf_names) or len(kf_names) != KF_COUNT:
        raise ValueError("kf names %d / id.h %d != %d"
                         % (len(kf_name_entries), len(kf_names), KF_COUNT))

    attr = text.split("kf_attr_tbl:", 1)[1].split("pf_name_tbl:", 1)[0]
    kf_attrs: list[tuple[int, int]] = []
    for line in attr.splitlines():
        line = line.split(";", 1)[0].strip()
        if not line:
            continue
        m = re.match(r"db\s+(.*)$", line, re.I)
        if not m:
            continue
        vals = [asm_int(v, kf_symbols) for v in split_asm_values(m.group(1))]
        if len(vals) != 2:
            raise ValueError("kf_attr_tbl row %r" % line)
        kf_attrs.append((vals[0], vals[1]))
    if len(kf_attrs) != KF_COUNT:
        raise ValueError("kf attrs %d != %d" % (len(kf_attrs), KF_COUNT))

    pf_body = body.split("pf_name_tbl:", 1)[1]
    pf_name_entries = re.findall(r"db\s+'([^']*)'\s*,\s*0ffh",
                                 pf_body.split("else", 1)[0])
    if len(pf_name_entries) != len(pf_names) or len(pf_names) != PF_COUNT:
        raise ValueError("pf names %d / id.h %d != %d"
                         % (len(pf_name_entries), len(pf_names), PF_COUNT))

    pf_attr = pf_body.split("pf_attr_tbl:", 1)[1]
    pf_kf: list[int] = []
    for line in pf_attr.splitlines():
        line = line.split(";", 1)[0].strip()
        if not line:
            continue
        m = re.match(r"db\s+(.*)$", line, re.I)
        if not m:
            continue
        for token in split_asm_values(m.group(1)):
            if token.strip().lower() in ("0ffh", "255"):
                continue
            pf_kf.append(asm_int(token, kf_symbols))
    if len(pf_kf) != PF_COUNT:
        raise ValueError("pf attrs %d != %d" % (len(pf_kf), PF_COUNT))

    kfs = []
    for i, name in enumerate(kf_names):
        kfs.append(dict(id=i, symbol=name, name=kf_name_entries[i],
                        type=kf_attrs[i][0], qi=kf_attrs[i][1]))
    pfs = []
    for i, name in enumerate(pf_names):
        pfs.append(dict(id=i, symbol=name, name=pf_name_entries[i],
                        kf=pf_kf[i]))

    # Spot checks anchored in skill.dat and id.h.
    if kfs[0]["name"] != "基本内功" or kfs[0]["type"] != 3:
        raise ValueError("kf 0 mismatch: %r" % (kfs[0],))
    if kfs[2]["name"] != "基本剑术" or kfs[2]["qi"] != 6:
        raise ValueError("kf 2 mismatch: %r" % (kfs[2],))
    if kfs[38]["name"] != "雪山剑法" or kfs[38]["type"] != 1:
        raise ValueError("kf 38 mismatch: %r" % (kfs[38],))
    if pfs[22]["name"] != "雪花六出" or pfs[22]["kf"] != 38:
        raise ValueError("pf 22 mismatch: %r" % (pfs[22],))
    if pfs[14]["name"] != "缠字决" or pfs[14]["kf"] != 30:
        raise ValueError("pf 14 mismatch: %r" % (pfs[14],))
    return kfs, pfs


def parse_goods_table(src_dir: str, idh: str) -> list[dict]:
    goods_path = os.path.join(src_dir, "goods.dat")
    src = read_utf8(goods_path)

    type_names = id_section(idh, ";goods_type", ";goods_id")
    item_names = id_section(idh, ";goods_id", ";weapon_type")
    weapon_names = id_section(idh, ";weapon_type", ";armor_type")
    armor_names = id_section(idh, ";armor_type", ";kf:")

    symbols: dict[str, int] = {}
    for names in (type_names, weapon_names, armor_names):
        for i, name in enumerate(names):
            symbols[name] = i
    for i, name in enumerate(item_names):
        symbols[name] = i
    # id.h aliases such as LONGSWORD equ LONG_SWORD.
    for alias, target in re.findall(r"^(\w+)\s+equ\s+(\w+)", idh, re.M):
        if target in symbols:
            symbols[alias] = symbols[target]

    body = src.split("goods_name_tbl:", 1)[1].split("goods_attr_tbl:", 1)[0]
    # The file carries a simplified (scode) and a traditional branch.
    branch = body.split("scode", 1)[1].split("else", 1)[0]
    names = re.findall(r"db\s+'([^']*)'\s*,\s*0ffh", branch)
    if len(names) != len(item_names):
        raise ValueError("item names %d != id.h goods ids %d" % (len(names), len(item_names)))
    if len(names) != ITEM_COUNT:
        raise ValueError("expected %d items, got %d" % (ITEM_COUNT, len(names)))

    # Stop at the skill tables that follow the item attributes.
    attr = src.split("goods_attr_tbl:", 1)[1].split("\nbook1_skill:", 1)[0]
    values: list[int] = []
    for line in attr.splitlines():
        line = line.split(";", 1)[0].strip()
        if not line:
            continue
        if re.fullmatch(r"db\s+0ffh", line, re.I):
            break
        m = re.match(r"(db|dw)\s+(.*)$", line, re.I)
        if not m:
            continue
        size = 1 if m.group(1).lower() == "db" else 2
        for token in split_asm_values(m.group(2)):
            v = asm_int(token, symbols)
            if size == 1:
                values.append(v & 0xFF)
            else:
                values.append(v & 0xFF)
                values.append((v >> 8) & 0xFF)
    if len(values) != len(names) * 8:
        raise ValueError("goods attr size %d, expected %d" % (len(values), len(names) * 8))

    items = []
    for i, name in enumerate(names):
        a = values[i * 8:(i + 1) * 8]
        items.append(dict(
            id=i, symbol=item_names[i], name=name, type=a[0], sub=a[1],
            effect=a[2], effect2=a[3],
            effect3=a[4], effect4=a[5],
            # goods.s wield_goods reads +3 as the 命中 bonus and +4 as the
            # 回避 bonus; effect2 doubles as food water, effect3 as defBonus.
            defBonus=a[4],
            price=a[6] | (a[7] << 8),
        ))

    def expect(idx, name, type_, sub, effect, price):
        it = items[idx]
        got = (it["name"], it["type"], it["sub"], it["effect"], it["price"])
        want = (name, type_, sub, effect, price)
        if got != want:
            raise ValueError("item %d mismatch: got %r, want %r" % (idx, got, want))

    expect(1, "烤鸡腿", 0, 0, 160, 30)
    expect(7, "白玉豆腐", 0, 0, 80, 35)
    expect(8, "金创药", 1, 0, 1, 2000)
    expect(13, "匕首", 2, 6, 5, 50)
    expect(42, "布衣", 3, 2, 5, 8)   # game.s clear_apply CLOTH
    expect(17, "长剑", 2, 6, 25, 600)
    expect(35, "铁剑", 2, 6, 30, 1000)
    expect(42, "布衣", 3, 2, 5, 8)
    expect(79, "屠龙刀", 2, 1, 88, 30000)
    return items


def parse_npc_table(src_dir: str, idh: str, items: list[dict],
                    kfs: list[dict]) -> list[dict]:
    # npc_data.gb is the complete source: 124 town NPCs followed by boss1..3.
    text = read_utf8(os.path.join(src_dir, "data", "npc_data.gb"))

    # h/id.h lists TRADE_PAI before the __base counter, so rebuild the list.
    symbols = dict(TRADE_PAI=0xFF, NONE_PAI=0, BAGUA_PAI=1, FLOWER_PAI=2, HONGLIAN_PAI=3,
                   NAJA_PAI=4, TAIJI_PAI=5, XUESHAN_PAI=6, XIAOYAO_PAI=7, BOSS_PAI=8)
    symbols.update({it["symbol"]: it["id"] for it in items})
    symbols.update({kf["symbol"]: kf["id"] for kf in kfs})
    for alias, target in re.findall(r"^(\w+)\s+equ\s+(\w+)", idh, re.M):
        if target in symbols:
            symbols[alias] = symbols[target]

    records = []
    cur = None
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith(";"):
            continue
        if s.endswith(":"):
            cur = [s[:-1], []]
            records.append(cur)
        elif cur is not None:
            cur[1].append(s)
    if len(records) != 127:
        raise ValueError("expected 127 npc records, got %d" % len(records))
    # The original bank has no record for KILLER_NPC: the ghost is built at
    # runtime. Keep a zeroed placeholder so ids 126..128 stay BOSS1..3.
    records.insert(124, ("killer", [
        "db\t'杀手',0,0,0",
        "db\t0,NONE_PAI,0,34,0",
        "db\t0,0,0,0",
        "dd\t0",
        "dw\t0",
        "db\t1,1,1,1,1,1",
        "dw\t1,1,0,0,1",
        "db\t0", "db\t0", "db\t0", "db\t0", "db\t0",
        "dw\t10",
        "db\t0", "db\t0", "db\t0", "db\t0", "db\t0",
        "db\t0",
        "db\t'阵法留下的虚影。',0",
    ]))
    if len(records) != NPC_RECORD_COUNT:
        raise ValueError("expected %d npc records, got %d" % (NPC_RECORD_COUNT, len(records)))

    def db_values(line: str) -> list[str]:
        m = re.match(r"db\s+(.*)$", line, re.I)
        if not m:
            raise ValueError("expected db: %r" % line)
        return split_asm_values(m.group(1))

    def dw_values(line: str) -> list[str]:
        m = re.match(r"(dd|dw)\s+(.*)$", line, re.I)
        if not m:
            raise ValueError("expected dw/dd: %r" % line)
        return split_asm_values(m.group(2))

    def ints(line: str) -> list[int]:
        return [asm_int(v, symbols) for v in db_values(line)]

    npcs = []
    merchant_count = 0
    for idx, (label, lines) in enumerate(records):
        if len(lines) < 20:
            raise ValueError("npc %s: too few lines (%d)" % (label, len(lines)))
        name_m = re.match(r"db\s+'([^']*)'", lines[0])
        if not name_m:
            raise ValueError("npc %s: missing name" % label)
        name = name_m.group(1)
        if len(name.encode("gb2312")) > 8:
            raise ValueError("npc %s: name does not fit 8 GB2312 bytes" % label)

        # npc_data.gb follows npc_state exactly:
        # busy,pai,gender,age,daode then attack,defense,damage,(armor=0).
        busy, pai, gender, age, daode = ints(lines[1])
        attack, defense, damage, _src_armor = ints(lines[2])
        exp = int(asm_int(dw_values(lines[3])[0], symbols))
        force = int(asm_int(dw_values(lines[4])[0], symbols))
        str_, dex, intel, con, per, kar = ints(lines[5])
        hp, maxhp, mp, maxmp, effhp = [int(asm_int(v, symbols)) for v in dw_values(lines[6])]
        goods = [asm_int(v, symbols) for v in
                 [db_values(lines[i])[0] for i in range(7, 12)]]
        money = int(asm_int(dw_values(lines[12])[0], symbols))
        usekf = [ints(lines[i])[0] for i in range(13, 18)]
        kfnum = ints(lines[18])[0]
        pos = 19
        kf = []
        for _ in range(kfnum):
            kf.append(ints(lines[pos]))
            pos += 1
        if kfnum > NPC_MAX_KF:
            raise ValueError("npc %s: kfnum %d > %d" % (label, kfnum, NPC_MAX_KF))
        for pair in kf:
            if len(pair) != 2 or pair[0] >= KF_COUNT or pair[1] > 255:
                raise ValueError("npc %s: bad kf pair %r" % (label, pair))

        # init_npc() in npc.s derives damage/armor from the equipped goods;
        # the source record itself has no armor byte.
        def equip_effect(raw: int, who: str) -> int:
            if not (raw & 0x80):
                return 0
            iid = raw & 0x7F
            if iid >= len(items):
                raise ValueError("npc %s: bad equipment id %d" % (who, iid))
            return items[iid]["effect"]

        equip_damage = equip_effect(goods[0], label)
        equip_armor = equip_effect(goods[1], label)

        vendors: list[int] = []
        if pai == 0xFF:
            merchant_count += 1
            count = ints(lines[pos])[0]
            pos += 1
            while len(vendors) < count:
                if pos >= len(lines):
                    raise ValueError("npc %s: vendor list truncated" % label)
                vendors.extend(ints(lines[pos]))
                pos += 1
            vendors = vendors[:count]
            for vid in vendors:
                if vid >= len(items):
                    raise ValueError("npc %s: bad vendor item %d" % (label, vid))

        desc_parts = []
        for line in lines[pos:]:
            m = re.match(r"db\s+'(.*)'\s*,\s*0\s*$", line)
            if m:
                desc_parts.append(m.group(1))
        desc = "".join(desc_parts)

        npcs.append(dict(
            id=idx + 1, label=label, name=name, busy=busy,
            pai=pai, gender=gender, age=age,
            daode=daode, attack=attack, defense=defense, baseDamage=damage,
            exp=exp, force=force,
            str=str_, dex=dex, intel=intel, con=con, per=per, kar=kar,
            hp=hp, maxhp=maxhp, mp=mp, maxmp=maxmp,
            weapon=goods[0], equip=goods[1],
            goods=goods,
            damage=damage + equip_damage, armor=_src_armor + equip_armor,
            usekf=usekf, kfnum=kfnum, kf=kf,
            money=money, vendor=vendors, desc=desc,
        ))

    by_label = {n["label"]: n for n in npcs}
    shops = {
        "aqing": [7, 4],
        "zahuofan": [18],
        "pingyizhi": [8, 9],
        "hetieshou": [22, 17, 13, 21, 11, 44],
        "gelangtai": [],
    }
    for label, want in shops.items():
        got = by_label[label]["vendor"]
        if got != want:
            raise ValueError("shop %s vendors %r != %r" % (label, got, want))
    if merchant_count != 13:
        raise ValueError("expected 13 merchants, got %d" % merchant_count)
    if by_label["hetieshou"]["id"] != 19:
        raise ValueError("hetieshou id %d" % by_label["hetieshou"]["id"])
    for label, npc_id, name in (("boss1", 126, "我是谁"),
                                ("boss2", 127, "东方求败"),
                                ("boss3", 128, "道德和尚")):
        if by_label[label]["id"] != npc_id or by_label[label]["name"] != name:
            raise ValueError("%s id/name mismatch: %r" % (label, by_label[label]))
    if by_label["killer"]["id"] != 125 or by_label["killer"]["pai"] != 0:
        raise ValueError("killer placeholder mismatch")
    return npcs


def _put_str(buf: bytearray, text: str, size: int) -> None:
    raw = text.encode("utf-8")
    if len(raw) >= size:
        raw = raw[:size - 1]
        while raw and (raw[-1] & 0xC0) == 0x80:
            raw = raw[:-1]
        raw = raw[:-1] if raw else raw
    buf.extend(raw + b"\x00" * (size - len(raw)))


def build_items_bin(items: list[dict]) -> bytes:
    """v2: +5 is the signed 回避 modifier; +30 holds the unused flags byte."""
    out = bytearray(b"GITM")
    out.extend(struct.pack("<HHHH", 2, len(items), ITEM_REC_SIZE, 0))
    for it in items:
        out.extend(struct.pack("<BBBBBBH", it["id"], it["type"], it["sub"],
                               it["effect"], it["effect2"], it["defBonus"],
                               it["price"]))
        _put_str(out, it["name"], 22)
        out.extend(b"\x00\x00")
    assert len(out) == 12 + ITEM_REC_SIZE * len(items)
    return bytes(out)


def build_npcs_bin(npcs: list[dict]) -> bytes:
    """v6 record (376 B): M3 fields plus kf/usekf and derived damage/armor."""
    out = bytearray(b"GNPC")
    out.extend(struct.pack("<HHHH", 6, len(npcs), NPC_REC_SIZE, 0))
    for n in npcs:
        out.extend(struct.pack("<13B", n["id"], n["pai"], n["gender"], n["age"],
                               n["str"], n["dex"], n["intel"], n["con"], n["per"], n["kar"],
                               n["weapon"] & 0x7F, n["equip"] & 0x7F,
                               len(n["vendor"])))
        out.extend(bytes((v & 0x7F) for v in n["vendor"])
                   + b"\x00" * (NPC_VENDOR_MAX - len(n["vendor"])))
        assert len(n["usekf"]) == NPC_MAX_USEKF
        out.append(n["kfnum"])
        out.extend(bytes((v & 0xFF) for v in n["usekf"]))
        flat = []
        for kid, level in n["kf"]:
            flat.extend((kid & 0xFF, level & 0xFF))
        out.extend(bytes(flat) + b"\x00" * (NPC_MAX_KF * 2 - len(flat)))
        out.extend(struct.pack("<HHHH", n["hp"], n["maxhp"], n["mp"], n["maxmp"]))
        out.extend(struct.pack("<HHH", n["money"], n["damage"], n["armor"]))
        out.extend(struct.pack("<I", n["exp"]))
        _put_str(out, n["name"], 24)
        # The description must survive whole: a truncated tail is a lost
        # sentence in 查看, so refuse to pack instead of cutting it.
        desc_bytes = len(n["desc"].encode("utf-8"))
        if desc_bytes >= NPC_DESC_LEN:
            raise ValueError("npc %s: desc is %d B, does not fit %d B"
                             % (n["label"], desc_bytes, NPC_DESC_LEN - 1))
        _put_str(out, n["desc"], NPC_DESC_LEN)
        out.extend(struct.pack("<HBB", n["force"], n["goods"][2],
                               (1 if n["weapon"] & 0x80 else 0) | (2 if n["equip"] & 0x80 else 0)))
        out.extend(bytes((n["goods"][3], n["daode"] & 0xff, n["attack"], n["defense"])))
        out.append(n["busy"] & 0xff)
    assert len(out) == 12 + NPC_REC_SIZE * len(npcs), len(out)
    return bytes(out)


def build_skills_bin(kfs: list[dict], pfs: list[dict]) -> bytes:
    """GSKL v1: 12 B header + 42 kungfu records + 24 perform records."""
    out = bytearray(b"GSKL")
    out.extend(struct.pack("<HHHH", 1, len(kfs), len(pfs), SKILL_REC_SIZE))
    for k in kfs:
        out.extend(struct.pack("<4B", k["id"], k["type"], k["qi"], 0))
        _put_str(out, k["name"], 20)
    for p in pfs:
        out.extend(struct.pack("<4B", p["id"], p["kf"], 0, 0))
        _put_str(out, p["name"], 20)
    assert len(out) == 12 + SKILL_REC_SIZE * (len(kfs) + len(pfs)), len(out)
    return bytes(out)


# ---------------------------------------------------------------- player sprites

PLAYER_FRAME_COUNT = 12
PLAYER_FRAME_W = 32
PLAYER_FRAME_H = 48
PLAYER_FRAME_BYTES = PLAYER_FRAME_W * PLAYER_FRAME_H // 8      # 192 B, 1bpp

# ref/gmud/src/player.s holds 24 frames: 12 images (img_player_*) and
# 12 AND masks (img_mask*), each 192 B. tools.s:426-429 + gmud.s encode the
# facing as way 0=down 1=left 2=right 3=up, and player_img_tbl (tools.s:433)
# maps index way*3+status onto the on-flash order: img_player_1..3 walk down,
# 4..6 left, 7..9 right, 10..12 up. Our engine orders frames by its own
# DIR_DOWN/LEFT/UP/RIGHT enum, so the pack below re-orders accordingly and the
# runtime lookup is simply frame = dir*3 + status.
PLAYER_IMG_LABEL_TO_SLOT = {
    "img_player_1": 0, "img_player_2": 1, "img_player_3": 2,     # down
    "img_player_4": 3, "img_player_5": 4, "img_player_6": 5,     # left
    "img_player_10": 6, "img_player_11": 7, "img_player_12": 8,  # up
    "img_player_7": 9, "img_player_8": 10, "img_player_9": 11,   # right
}


def parse_player_sprites(src_dir: str):
    """Parse src/player.s into 12 image frames + 12 mask frames (192 B each).

    Returns (frames, masks) as lists of 12 bytes objects indexed by
    dir*3 + status with the engine's DIR_ order.
    """
    text = open(os.path.join(src_dir, "player.s"), "r", encoding="utf-8").read()
    label_re = re.compile(r"^([A-Za-z_]\w*):\s*$")
    db_re = re.compile(r"^\s*db\s+(.*)$")
    val_re = re.compile(r"([0-9A-Fa-f]+)h\b")

    sections: dict[str, bytearray] = {}
    cur: str | None = None
    for line in text.splitlines():
        m = label_re.match(line)
        if m:
            cur = m.group(1)
            sections[cur] = bytearray()
            continue
        m = db_re.match(line)
        if m and cur:
            vals = val_re.findall(m.group(1))
            if not vals:
                raise ValueError("player.s: db line without 0xxh bytes: %r" % line)
            sections[cur].extend(int(v, 16) for v in vals)

    frames: list[bytes | None] = [None] * PLAYER_FRAME_COUNT
    masks: list[bytes | None] = [None] * PLAYER_FRAME_COUNT
    for label, data in sections.items():
        if len(data) != PLAYER_FRAME_BYTES:
            raise ValueError("player.s: section %s is %d B, expected %d"
                             % (label, len(data), PLAYER_FRAME_BYTES))
        slot = PLAYER_IMG_LABEL_TO_SLOT.get(label)
        if slot is not None:
            frames[slot] = bytes(data)
        elif label.startswith("img_mask"):
            # img_mask<N> pairs with img_player_<N>; N>9 keeps the same digits.
            src = "img_player_" + label[len("img_mask"):]
            mask_slot = PLAYER_IMG_LABEL_TO_SLOT.get(src)
            if mask_slot is None:
                raise ValueError("player.s: mask %s has no image counterpart" % label)
            masks[mask_slot] = bytes(data)
        else:
            raise ValueError("player.s: unexpected label %s" % label)

    for i in range(PLAYER_FRAME_COUNT):
        if frames[i] is None or masks[i] is None:
            raise ValueError("player.s: missing frame or mask at slot %d" % i)
    return frames, masks


def build_player_bin(frames: list[bytes], masks: list[bytes]) -> bytes:
    """PSPR v1: 16 B header + 12 image frames + 12 AND masks.

    Header: "PSPR", u16 version=1, u16 frameCount, u16 frameW, u16 frameH,
            u16 frameBytes, u16 flags.
    Frames are indexed dir*3 + status (engine DIR_ order); a mask bit of 1
    keeps the background (transparent), matching the tile AND-mask semantics.
    """
    out = bytearray(b"PSPR")
    out.extend(struct.pack("<HHHHHH", 1, PLAYER_FRAME_COUNT, PLAYER_FRAME_W,
                           PLAYER_FRAME_H, PLAYER_FRAME_BYTES, 0))
    for f in frames:
        out.extend(f)
    for m in masks:
        out.extend(m)
    assert len(out) == 16 + 2 * PLAYER_FRAME_COUNT * PLAYER_FRAME_BYTES, len(out)
    return bytes(out)


def write_player_sheet(path: str, frames: list[bytes], masks: list[bytes]) -> None:
    """player.png: 4 columns (DIR_DOWN/LEFT/UP/RIGHT) x 3 walk frames, 32x48."""
    cols, rows_n = 4, 3
    width = cols * PLAYER_FRAME_W
    height = rows_n * PLAYER_FRAME_H
    rows = [bytearray(width * 4) for _ in range(height)]

    def px(x: int, y: int, rgba: tuple[int, int, int, int]) -> None:
        rows[y][x * 4:x * 4 + 4] = bytes(rgba)

    for frame in range(PLAYER_FRAME_COUNT):
        col = frame // rows_n
        row = frame % rows_n
        data = frames[frame]
        mask = masks[frame]
        for y in range(PLAYER_FRAME_H):
            for x in range(PLAYER_FRAME_W):
                off = y * 4 + x // 8
                shift = 7 - (x & 7)
                ink = (data[off] >> shift) & 1
                keep = (mask[off] >> shift) & 1
                gx = col * PLAYER_FRAME_W + x
                gy = row * PLAYER_FRAME_H + y
                if ink:
                    px(gx, gy, INK + (255,))
                elif not keep:
                    px(gx, gy, PAPER + (255,))
                # keep == 1 -> transparent, stays zeroed

    write_png(path, width, height, [bytes(r) for r in rows], alpha=True)


# ------------------------------------------------------------- npc sprites

NPC_SPR_SLOTS = 128            # img_npc_tbl entries, indexed by npcId (0 = none)


def _asm_value(token: str) -> int | None:
    """Parse one assembler literal: 0ffh / 16 / -5. Returns None for labels."""
    tok = token.strip()
    if tok.endswith(("h", "H")) and tok[0] in "0123456789":
        return int(tok[:-1], 16)
    try:
        return int(tok, 10)
    except ValueError:
        return None


def parse_npc_sprites(src_dir: str):
    """img_addr.s img_npc_tbl -> 128 auxiliary images from src/data/image_data.

    Each portrait is `db w,h` + h rows of (w+7)//8 bytes, 1bpp. The table is
    indexed by npcId (0 = none); img_boy*/img_girl* entries are shared between
    ids. The original fight screen uses the map placement's G_id_item instead.
    Returns a list of 128 (w, h, data) tuples with (0, 0, b"") for empty.
    """
    addr_text = open(os.path.join(src_dir, "img_addr.s"), "r", encoding="utf-8").read()
    m = re.search(r"^img_npc_tbl:", addr_text, re.M)
    if not m:
        raise ValueError("img_addr.s: img_npc_tbl not found")
    rest = addr_text[m.end():]
    stop = re.search(r"^\w+:", rest, re.M)
    table = []
    for line in rest[:stop.start() if stop else len(rest)].splitlines():
        dm = re.match(r"^\s*dw\s+(.*)$", line)
        if dm:
            body = dm.group(1).split(";")[0]
            for tok in body.split(","):
                tok = tok.strip()
                if tok == "0":
                    table.append(None)
                elif tok:
                    table.append(tok)
    if len(table) != NPC_SPR_SLOTS:
        raise ValueError("img_npc_tbl has %d entries, expected %d"
                         % (len(table), NPC_SPR_SLOTS))

    img_text = open(os.path.join(src_dir, "data", "image_data"), "r",
                    encoding="utf-8").read()
    wanted = {name for name in table if name}
    label_re = re.compile(r"^(\w+):\s*$")
    db_re = re.compile(r"^\s*db\s+(.*)$")
    images: dict[str, tuple[int, int, bytes]] = {}
    pending = None
    dim = None
    bpr = 0
    rows: list[bytes] = []
    for line in img_text.splitlines():
        lm = label_re.match(line)
        if lm:
            # only the 16x16 portraits referenced by img_npc_tbl are parsed;
            # other shapes (img_magic is 31x31 with its own packing) are skipped
            pending = lm.group(1) if lm.group(1) in wanted else None
            dim = None
            rows = []
            continue
        dm = db_re.match(line)
        if not dm or pending is None:
            continue
        toks = [t.strip() for t in dm.group(1).split(",")]
        vals = [_asm_value(t) for t in toks]
        if any(v is None for v in vals):
            continue                       # not a pure data line
        if dim is None:
            if len(vals) != 2 or vals[0] <= 0 or vals[1] <= 0 or vals[0] > 64 or vals[1] > 64:
                raise ValueError("image_data: bad header for %s: %r" % (pending, line))
            dim = (vals[0], vals[1])
            bpr = (vals[0] + 7) // 8
            continue
        if len(vals) != bpr:
            raise ValueError("image_data: %s row has %d values, expected %d"
                             % (pending, len(vals), bpr))
        rows.append(bytes(v for v in vals))
        if len(rows) == dim[1]:
            images[pending] = (dim[0], dim[1], b"".join(rows))
            pending = None
            dim = None
            rows = []
    missing = wanted - set(images)
    if missing:
        raise ValueError("image_data: missing referenced images: %s"
                         % ", ".join(sorted(missing)))

    out: list[tuple[int, int, bytes]] = []
    for name in table:
        if name is None:
            out.append((0, 0, b""))
            continue
        if name not in images:
            raise ValueError("image_data: %s (referenced by img_npc_tbl) missing" % name)
        out.append(images[name])
    return out


def build_npcspr_bin(sprites) -> bytes:
    """NSPR v1: 8 B header ("NSPR", u16 version, u16 slotCount) + 128 slots
    of (u8 w, u8 h, rowbytes*h data), indexed by npcId (0 = none)."""
    out = bytearray(b"NSPR")
    out.extend(struct.pack("<HH", 1, NPC_SPR_SLOTS))
    for w, h, data in sprites:
        out.append(w)
        out.append(h)
        out.extend(data)
    return bytes(out)


def write_npcspr_sheet(path: str, sprites) -> None:
    """npcspr.png: 16 columns x 8 rows of 16x16 portraits (npcId order)."""
    cols, rows_n = 16, 8
    width = cols * 16
    height = rows_n * 16
    rows = [bytearray(width * 4) for _ in range(height)]
    for slot, (w, h, data) in enumerate(sprites):
        if w == 0:
            continue
        col = slot % cols
        row = slot // cols
        bpr = (w + 7) // 8
        for y in range(h):
            for x in range(w):
                bit = (data[y * bpr + x // 8] >> (7 - (x & 7))) & 1
                if bit:
                    gx, gy = col * 16 + x, row * 16 + y
                    rows[gy][gx * 4:gx * 4 + 4] = bytes(INK + (255,))
    write_png(path, width, height, [bytes(r) for r in rows], alpha=True)


# ------------------------------------------------------------- fight text

def _asm_strings(line: str) -> list[str]:
    """All quoted '...' segments on a db line (commas inside are literal)."""
    return re.findall(r"'([^']*)'", line)


def collect_label_text(text: str, label: str, what: str) -> str:
    """Concatenated '...' segments of an assembler label's db text.

    Handles both `label	db '...',0` and
    `label:` followed by db lines (kf_data.gb style); the label name is
    anchored at line start so plain db rows cannot match.
    """
    lab_re = re.compile(r"^%s\s*:?\s*(?:db\s+(.*))?$" % re.escape(label))
    segs: list[str] = []
    in_label = False
    for line in text.splitlines():
        m = lab_re.match(line)
        if m:
            in_label = True
            rest = m.group(1)
            if rest is not None:
                segs.extend(_asm_strings(rest))
                if re.search(r",\s*0\s*$", rest):
                    break
            continue
        if in_label:
            dm = re.match(r"^\s*db\s+(.*)$", line)
            if dm:
                segs.extend(_asm_strings(dm.group(1)))
                if re.search(r",\s*0\s*$", dm.group(1)):
                    break
            elif segs:
                break
    if not segs:
        raise ValueError("%s: label %s has no text" % (what, label))
    return "".join(segs)


def parse_fight_text(src_dir: str) -> dict:
    """Collect original battle strings for the V1 fight log.

    Sources (simplified-chinese `if scode` branches only):
      - string.s limbs_data: 16 body-part names ($l / $1)
      - text_addr.s: bare/sword/blade/club/staff/whip attack templates
        (db N,0 header, then N x [db damage_type, dw label]; dodge has bare
        dw) and the 42-entry kf_addr_tbl (index = kungfu id - 1)
      - data/kf_data.gb: per-move records (3+3 db attr, dw damage/force,
        dw desc label) and the desc strings themselves
    Returns dict with limbs, tables {name: [(damage_type, text)]}, kf {id: [text]}.
    """
    # 1) limbs
    string_s = open(os.path.join(src_dir, "string.s"), "r", encoding="utf-8").read()
    m = re.search(r"^limbs_data:\s*$(.*?)(?=^\s*else\s*$)", string_s, re.M | re.S)
    if not m:
        raise ValueError("string.s: limbs_data not found")
    limbs: list[str] = []
    for line in m.group(1).splitlines():
        for seg in _asm_strings(line):
            limbs.append(seg)
    if len(limbs) != 16:
        raise ValueError("string.s: limbs_data has %d entries, expected 16" % len(limbs))

    # 2) attack/dodge templates
    ta = open(os.path.join(src_dir, "text_addr.s"), "r", encoding="utf-8").read()
    tbl_names = ["bare_msg_tbl", "sword_msg_tbl", "blade_msg_tbl", "club_msg_tbl",
                 "staff_msg_tbl", "whip_msg_tbl", "dodge_msg_tbl"]
    tables: dict[str, list[tuple[int, str]]] = {}
    for name in tbl_names:
        m = re.search(r"^%s:\s*$(.*?)(?=^\w+:)" % name, ta, re.M | re.S)
        if not m:
            raise ValueError("text_addr.s: %s not found" % name)
        body = m.group(1)
        # header db N,0
        hm = re.search(r"^\s*db\s+(\d+)\s*,\s*0\s*$", body, re.M)
        if not hm:
            raise ValueError("text_addr.s: %s header missing" % name)
        n = int(hm.group(1))
        body_rest = body[hm.end():]
        # cut at scode branch: labels are defined there, not part of the table
        cut = re.search(r"^\s*if\s+scode", body_rest, re.M)
        if cut:
            body_rest = body_rest[:cut.start()]
        entries: list[tuple[int, str]] = []
        pending_type = None
        for line in body_rest.splitlines():
            dm = re.match(r"^\s*db\s+(.*)$", line)
            wm = re.match(r"^\s*dw\s+(.*)$", line)
            if dm:
                vals = [_asm_value(t) for t in dm.group(1).split(",")]
                if any(v is None for v in vals):
                    continue
                pending_type = vals[0]
            elif wm:
                label = wm.group(1).strip()
                if _asm_value(label) is not None:
                    continue
                entries.append((pending_type if pending_type is not None else 0, label))
                pending_type = None
        if len(entries) != n:
            raise ValueError("text_addr.s: %s has %d entries, expected %d"
                             % (name, len(entries), n))
        tables[name] = entries

    # resolve template texts from the scode branch of text_addr.s
    def label_text(label: str) -> str:
        return collect_label_text(ta, label, "text_addr.s")

    tables_text = {name: [(damage_type, label_text(label))
                          for damage_type, label in entries]
                   for name, entries in tables.items()}

    # 3) kf_addr_tbl -> kungfu move descriptions in kf_data.gb
    km = re.search(r"^kf_addr_tbl:\s*;(.*?)(?=^\w+:)", ta, re.M | re.S)
    if not km:
        raise ValueError("text_addr.s: kf_addr_tbl not found")
    kf_addrs: list[str | None] = []
    for line in km.group(1).splitlines():
        wm = re.match(r"^\s*dw\s+(.*)$", line)
        if wm:
            tok = wm.group(1).split(";")[0].strip()
            kf_addrs.append(None if tok == "0" else tok)
    # 41 entries: 0=force 1=bare 2=sword 3=blade 4=club 5=staff 6=whip
    # 7=dodge, 8..10 empty, 11..40 = baguad..menghu_quan. The index matches
    # the engine's 0-based SK_* kungfu id exactly (skill.h enum); SK_XI(41)
    # has no fight templates.
    if len(kf_addrs) != 41:
        raise ValueError("text_addr.s: kf_addr_tbl has %d entries, expected 41"
                         % len(kf_addrs))

    kf_text = open(os.path.join(src_dir, "data", "kf_data.gb"), "r",
                   encoding="utf-8").read()
    kf_moves: dict[int, list[str]] = {}
    kf_attrs: dict[int, list[list[int]]] = {}
    for kf_id, addr in enumerate(kf_addrs):
        if addr is None or kf_id <= 7:
            continue   # 0=force, 1..6 attack tables, 7=dodge (all in tables)
        am = re.search(r"^%s:\s*$(.*?)(?=^\w+:)" % addr, kf_text, re.M | re.S)
        if not am:
            raise ValueError("kf_data.gb: %s not found" % addr)
        body = am.group(1)
        hm = re.search(r"^\s*db\s+(\d+)\s*,\s*0\s*(?:;.*)?$", body, re.M)
        if not hm:
            # e.g. hunyuan_addr is an empty table (force kungfu, no fight text)
            has_data = any(re.match(r"^\s*d[bw]\s+", ln) for ln in body.splitlines())
            if not has_data:
                kf_moves[kf_id] = []
                continue
            raise ValueError("kf_data.gb: %s header missing" % addr)
        n = int(hm.group(1))
        rest = body[hm.end():]
        # two layouts: dodge-style tables are just N dw desc labels; attack
        # kungfu carry 10-byte records (3+3 attr, damage/force, desc label)
        first = next(ln for ln in rest.splitlines() if re.match(r"^\s*d[bw]\s+", ln))
        if re.match(r"^\s*dw\s+\D", first):
            moves = []
            attrs = []
            for line in rest.splitlines():
                wm = re.match(r"^\s*dw\s+(.*)$", line)
                if wm:
                    lbl = wm.group(1).split(";")[0].strip()
                    if _asm_value(lbl) is None:
                        moves.append(lbl)
            if len(moves) != n:
                raise ValueError("kf_data.gb: %s has %d labels, expected %d"
                                 % (addr, len(moves), n))
        else:
            vals: list = []
            moves: list[str] = []
            attrs = []
            for line in rest.splitlines():
                dm = re.match(r"^\s*db\s+(.*)$", line)
                wm = re.match(r"^\s*dw\s+(.*)$", line)
                if not dm and not wm:
                    continue
                toks = [t.strip() for t in (dm or wm).group(1).split(",")]
                for t in toks:
                    v = _asm_value(t)
                    vals.append(v if v is not None else t)
                # a move record = 8 numeric fields (3+3 attr, damage, force) + desc
                while len(vals) >= 9:
                    desc = vals[8]
                    if not isinstance(desc, str):
                        raise ValueError("kf_data.gb: %s record without desc label" % addr)
                    moves.append(desc)
                    attrs.append(vals[:8])
                    vals = vals[9:]
            if len(moves) != n:
                raise ValueError("kf_data.gb: %s has %d moves, expected %d"
                                 % (addr, len(moves), n))

        kf_moves[kf_id] = [collect_label_text(kf_text, lbl, "kf_data.gb")
                           for lbl in moves]
        kf_attrs[kf_id] = attrs

    return {"limbs": limbs, "tables": tables_text, "kf": kf_moves, "attrs": kf_attrs}


# ------------------------------------------------- string2.s hit/result text

def _scode_label_text(text: str) -> dict[str, str]:
    """label -> simplified db text for the `if scode` blocks of string2.s.

    A label with no db of its own (t1damage120_msg, t1damage240_msg,
    t2damage120_msg, t2damage240_msg) aliases the next defined string: the
    assembler emits two labels at one address. Those slots therefore share
    the 160 / big text, giving 44 distinct strings for 6 x 8 damage slots.
    """
    out: dict[str, str] = {}
    pending: list[str] = []
    in_scode = False
    for line in text.splitlines():
        raw = line.rstrip()
        stripped = raw.strip()
        if re.match(r"^if\s+scode\b", stripped):
            in_scode, pending = True, []
            continue
        if re.match(r"^(else|endif)\b", stripped):
            in_scode, pending = False, []
            continue
        if not in_scode or not stripped or stripped.startswith(";"):
            continue
        label = None
        rest = None
        if not raw[0].isspace():                 # labels start in column 0
            lm = re.match(r"^(\w+)\s*:?\s*(?:db\s+(.*))?$", raw)
            if lm:
                label, rest = lm.group(1), lm.group(2)
        if label:
            pending.append(label)
        if rest is None:
            dm = re.match(r"^\s*db\s+(.*)$", raw)
            rest = dm.group(1) if dm else None
        if rest is None:
            continue
        segs = _asm_strings(rest)
        if not segs:
            continue
        value = "".join(segs)
        for lab in pending:
            out[lab] = value
        pending = []
    return out


def _asm_db_values(text: str, label: str) -> list[int]:
    """Numeric db rows of one label up to the next column-0 label."""
    m = re.search(r"^%s:[^\n]*$(.*?)(?=^\w+:)" % re.escape(label),
                  text, re.M | re.S)
    if not m:
        raise ValueError("string2.s: %s not found" % label)
    values: list[int] = []
    for line in m.group(1).splitlines():
        dm = re.match(r"^\s*db\s+(.*)$", line)
        if not dm:
            continue
        for tok in dm.group(1).split(","):
            value = _asm_value(tok)
            if value is None:
                raise ValueError("string2.s: %s has non-numeric db %r" % (label, tok))
            values.append(value)
    return values


def _asm_dw_tables(text: str, start: str, stop: str) -> dict[str, list[str]]:
    """{table label: [dw label, ...]} for the consecutive dw tables."""
    m = re.search(r"^(%s:[^\n]*\n.*?)(?=%s)" % (re.escape(start), stop),
                  text, re.M | re.S)
    if not m:
        raise ValueError("string2.s: %s table block not found" % start)
    tables: dict[str, list[str]] = {}
    for tm in re.finditer(r"^(\w+):[^\n]*\n((?:[ \t]+dw[ \t]+\w+[^\n]*\n)*)",
                          m.group(1), re.M):
        tables[tm.group(1)] = re.findall(r"^\s*dw\s+(\w+)", tm.group(2), re.M)
    return tables


def parse_result_text(src_dir: str) -> dict:
    """string2.s: get_damage_msg / get_result_msg / get_status_msg tables.

    Everything is read from the assembly: the 6 x 8 damage_msg_tbl labels,
    damage0_msg, result_tip, the two 11-entry status tables and both
    threshold rows. Nothing is transcribed by hand.
    """
    text = open(os.path.join(src_dir, "string2.s"), "r", encoding="utf-8").read()
    if not re.search(r"^MAX_TYPE\s+equ\s+6\b", text, re.M):
        raise ValueError("string2.s: MAX_TYPE is no longer 6")
    labels = _scode_label_text(text)

    def need(label: str) -> str:
        if label not in labels:
            raise ValueError("string2.s: label %s has no scode text" % label)
        return labels[label]

    damage_tables = _asm_dw_tables(text, "damage_msg_tbl", r"^\s*if\s+scode")
    status_tables = _asm_dw_tables(text, "status_msg_tbl", r"^\s*if\s+scode")
    type_names = damage_tables.get("damage_msg_tbl", [])
    if len(type_names) != 6:
        raise ValueError("string2.s: damage_msg_tbl has %d types, expected 6" % len(type_names))
    damage = [[need(lbl) for lbl in damage_tables.get(name, [])] for name in type_names]
    status = [need(lbl) for lbl in status_tables.get("status_msg_tbl", [])]
    eff_status = [need(lbl) for lbl in status_tables.get("eff_status_tbl", [])]
    if [len(row) for row in damage] != [8] * 6:
        raise ValueError("string2.s: damage tables must have 8 slots each")
    if len(status) != 11 or len(eff_status) != 11:
        raise ValueError("string2.s: status tables must have 11 slots each")
    damage_data = _asm_db_values(text, "damage_msg_data")
    status_data = _asm_db_values(text, "status_msg_data")
    if damage_data != [10, 20, 40, 80, 120, 160, 240, 255]:
        raise ValueError("string2.s: damage_msg_data changed: %r" % damage_data)
    if status_data != [99, 95, 90, 80, 60, 40, 30, 20, 10, 5, 0]:
        raise ValueError("string2.s: status_msg_data changed: %r" % status_data)
    return {
        "damage": damage, "damage0": need("damage0_msg"), "tip": need("result_tip"),
        "status": status, "eff_status": eff_status,
        "damage_data": damage_data, "status_data": status_data,
    }


# ------------------------------------------------ perform.txt full label table

def parse_perform_text(src_dir: str) -> dict:
    """perform.txt's 73 simplified labels and the pf -> primary label table.

    The label index is perform_tbl's dw order, which is also the
    `define 1,...` order in h/perform_id.h: perform.s show_text takes that
    index in a9 and looks the address up through txt_class_tbl. The 24-byte
    pf -> primary table is derived from hello.s perform_tbl (24 entry points)
    and the first `lm2 a9,#..._sucess_msg` of each perform.s routine, so no
    mapping is transcribed by hand. Only the `if scode` (simplified) block is
    read; the `else` block is the traditional-chinese mirror.
    """
    text = open(os.path.join(src_dir, "data", "perform.txt"), "r",
                encoding="utf-8").read()
    tm = re.search(r"^perform_tbl:[^\n]*\n((?:[ \t]*dw[ \t]+\w+[^\n]*\n)+)",
                   text, re.M)
    if not tm:
        raise ValueError("perform.txt: perform_tbl not found")
    labels = re.findall(r"^[ \t]*dw[ \t]+(\w+)", tm.group(1), re.M)
    if len(labels) != 73 or len(set(labels)) != 73:
        raise ValueError("perform.txt: perform_tbl has %d labels (%d unique), expected 73"
                         % (len(labels), len(set(labels))))

    idh = open(os.path.join(src_dir, "h", "perform_id.h"), "r",
               encoding="utf-8").read()
    defines = re.findall(r"^\s*define\s+1,(\w+)", idh, re.M)
    if defines != labels:
        raise ValueError("perform_id.h: define order differs from perform_tbl")

    lines = text.splitlines()
    start = next((i for i, line in enumerate(lines)
                  if re.match(r"^\s*if\s+scode\b", line)), None)
    if start is None:
        raise ValueError("perform.txt: simplified if scode block not found")
    end = next((i for i, line in enumerate(lines[start + 1:], start + 1)
                if re.match(r"^\s*else\b", line)), None)
    if end is None:
        raise ValueError("perform.txt: no else after the simplified block")
    simplified = "\n".join(lines[start + 1:end])
    texts = [collect_label_text(simplified, label, "perform.txt")
             for label in labels]
    if not all(texts):
        raise ValueError("perform.txt: an scode label has no text")
    index = {name: i for i, name in enumerate(labels)}

    hello = open(os.path.join(src_dir, "hello.s"), "r", encoding="utf-8").read()
    funcs = re.findall(r"^\s*DEF_BREAK_FUN\s+(\w+)", hello, re.M)
    if len(funcs) != 24:
        raise ValueError("hello.s: perform_tbl has %d entries, expected 24" % len(funcs))
    asm = open(os.path.join(src_dir, "perform.s"), "r", encoding="utf-8").read()
    marks = sorted((m.start(), fn) for fn in funcs
                   for m in re.finditer(r"^%s:" % re.escape(fn), asm, re.M))
    if len(marks) != 24:
        raise ValueError("perform.s: only %d of 24 perform_tbl routines found" % len(marks))
    first_label = {}
    for i, (pos, fn) in enumerate(marks):
        body = asm[pos:marks[i + 1][0] if i + 1 < len(marks) else len(asm)]
        refs = re.findall(r"\blm2\s+a9,#(\w+)", body)
        if not refs:
            raise ValueError("perform.s: %s has no show_text label" % fn)
        first_label[fn] = refs[0]
    primary: list[int] = []
    for fn in funcs:
        label = first_label[fn]
        if label not in index or not label.endswith("_sucess_msg"):
            raise ValueError("perform.s: %s primary %s is not a perform_tbl success label"
                             % (fn, label))
        primary.append(index[label])
    return {"labels": labels, "texts": texts, "primary": primary}


# fighttext.bin entry ids (u16)
FT_ID_LIMB = 0            # 0..15
FT_ID_BARE = 100          # 100..104
FT_ID_SWORD = 200         # 200..202
FT_ID_BLADE = 210
FT_ID_CLUB = 220
FT_ID_STAFF = 230
FT_ID_WHIP = 240          # 240
FT_ID_DODGE = 300         # 300..304
FT_ID_KF_BASE = 1000      # + kfId*24 + moveIdx: max 1979, keeps 2000+ free
FT_KF_STRIDE = 24
FT_ID_PERFORM_LABEL = 2000  # 2000 + perform_id.h label index (0..72)
FT_PERFORM_LABELS = 73
FT_PERFORM_MAX = 24        # hello.s perform_tbl entry count
FT_ID_DAMAGE = 2100       # 2100 + damage_type*8 + level
FT_ID_DAMAGE0 = 2148
FT_ID_RESULT_TIP = 2149   # '结果' prefix of get_result_msg
FT_ID_STATUS = 2200       # 2200..2210
FT_ID_EFF_STATUS = 2211   # 2211..2221
FT_THRESHOLD_BYTES = 8 + 11
FT_PRIMARY_BYTES = FT_PERFORM_MAX  # u8[24] pf -> primary label index


def build_fighttext_bin(ft: dict, result: dict,
                        perform: dict) -> tuple[bytes, list[str]]:
    """FTXT v5: header + 43 B move counts + move metadata + 19 B thresholds
    + u8[24] pf->primary label table + entries sorted by unique u16 id.

    Entry = u16 id, u8 attr, u8 len, UTF-8 bytes + NUL. For attack templates
    attr is the original `db damage_type` byte (string.s is_basic_msg loads
    it into damage_type); 255 marks the uniform-pick dodge / kungfu entries.
    v5 adds all 73 perform.txt labels at 2000+index and drops v4's duplicated
    3000+ primary/branch slots: every text is addressed by its original
    perform_id.h index, so a 3~4 way branch needs no extra slot per perform.
    """
    entries: list[tuple[int, int, str]] = []
    for i, text in enumerate(ft["limbs"]):
        entries.append((FT_ID_LIMB + i, 255, text))
    tbl = ft["tables"]
    for base, name in ((FT_ID_BARE, "bare_msg_tbl"), (FT_ID_SWORD, "sword_msg_tbl"),
                       (FT_ID_BLADE, "blade_msg_tbl"), (FT_ID_CLUB, "club_msg_tbl"),
                       (FT_ID_STAFF, "staff_msg_tbl"), (FT_ID_WHIP, "whip_msg_tbl")):
        for i, (damage_type, text) in enumerate(tbl[name]):
            entries.append((base + i, damage_type, text))
    for i, (_damage_type, text) in enumerate(tbl["dodge_msg_tbl"]):
        entries.append((FT_ID_DODGE + i, 255, text))
    kf_ids = sorted(ft["kf"])
    for kf_id in kf_ids:
        moves = ft["kf"][kf_id]
        if len(moves) >= FT_KF_STRIDE:
            raise ValueError("kungfu %d has %d moves, stride is %d"
                             % (kf_id, len(moves), FT_KF_STRIDE))
        for i, text in enumerate(moves):
            entries.append((FT_ID_KF_BASE + kf_id * FT_KF_STRIDE + i, 255, text))

    # string2.s: 6 damage types x 8 slots, damage0, the '结果' prefix and
    # both 11-slot status tables. Empty labels already resolve to the shared
    # 160 / big text, so all 48 slots are directly addressable.
    for dtype, row in enumerate(result["damage"]):
        for level, text in enumerate(row):
            entries.append((FT_ID_DAMAGE + dtype * 8 + level, 255, text))
    entries.append((FT_ID_DAMAGE0, 255, result["damage0"]))
    entries.append((FT_ID_RESULT_TIP, 255, result["tip"]))
    for i, text in enumerate(result["status"]):
        entries.append((FT_ID_STATUS + i, 255, text))
    for i, text in enumerate(result["eff_status"]):
        entries.append((FT_ID_EFF_STATUS + i, 255, text))

    # perform.txt: all 73 simplified labels at 2000 + perform_id.h index.
    # Branch messages are no longer copied into per-perform 3000+ slots; the
    # C side addresses the original label directly (PF_MSG_* constants).
    if len(perform["labels"]) != FT_PERFORM_LABELS or len(perform["texts"]) != FT_PERFORM_LABELS:
        raise ValueError("fighttext.bin: perform label table must have 73 entries")
    if len(perform["primary"]) != FT_PRIMARY_BYTES or any(
            v >= FT_PERFORM_LABELS for v in perform["primary"]):
        raise ValueError("fighttext.bin: pf -> primary label table is invalid")
    for i, text in enumerate(perform["texts"]):
        entries.append((FT_ID_PERFORM_LABEL + i, 255, text))

    entries.sort(key=lambda e: e[0])
    ids = [e[0] for e in entries]
    if len(set(ids)) != len(ids):
        raise ValueError("fighttext.bin: duplicate entry id")
    if any(FT_ID_PERFORM_LABEL + FT_PERFORM_LABELS <= ident < FT_ID_DAMAGE
           for ident in ids):
        raise ValueError("fighttext.bin: 2073..2099 must stay reserved")
    if not all(FT_ID_PERFORM_LABEL + i in ids for i in range(FT_PERFORM_LABELS)):
        raise ValueError("fighttext.bin: a perform.txt label entry is missing")
    if len(entries) > 0xFFFF:
        raise ValueError("fighttext.bin: too many entries")

    out = bytearray(b"FTXT")
    out.extend(struct.pack("<HH", 5, len(entries)))
    for kf_id in range(0, 43):            # u8 moveCount[kfId 0..42]
        out.append(len(ft["kf"].get(kf_id, ())))
    for kf_id in range(43):
        attrs = ft["attrs"].get(kf_id, [])
        for i in range(len(ft["kf"].get(kf_id, ()))):
            values = attrs[i] if i < len(attrs) else [0] * 8
            out.extend(struct.pack("<6BHH", *(v & 255 for v in values[:6]), values[6] & 65535, values[7] & 65535))
    # string2.s damage_msg_data + status_msg_data, used by the runtime's
    # get_damage_msg / get_status_msg thresholds instead of C literals.
    out.extend(bytes(result["damage_data"]))
    out.extend(bytes(result["status_data"]))
    assert len(result["damage_data"]) + len(result["status_data"]) == FT_THRESHOLD_BYTES
    # u8[24] pf -> primary perform.txt label index (hello.s perform_tbl order).
    out.extend(bytes(perform["primary"]))
    assert len(perform["primary"]) == FT_PRIMARY_BYTES
    for ident, attr, text in entries:
        raw = text.encode("utf-8")
        if len(raw) > 255:
            raise ValueError("fight text too long (id %d): %d B" % (ident, len(raw)))
        out.extend(struct.pack("<HBB", ident, attr & 0xFF, len(raw)))
        out.extend(raw)
        out.append(0)   # NUL terminator: the runtime reads these as C strings
    return bytes(out), [e[2] for e in entries]


# ------------------------------------------------------------------------ pack


def build_pack(world) -> bytes:
    maps = world["maps"]
    doors = world["doors"]
    npcs = world["npcs"]

    off_maps = HEADER_SIZE
    off_doors = off_maps + MAP_ENTRY_SIZE * len(maps)
    off_npcs = off_doors + 6 * len(doors)
    off_cells = off_npcs + 3 * len(npcs)

    header = bytearray(HEADER_SIZE)
    header[0:4] = b"GM3D"
    struct.pack_into("<HHHH", header, 4, 1, len(maps), len(doors), len(npcs))
    struct.pack_into("<IIII", header, 12, off_maps, off_doors, off_npcs, off_cells)
    start_map, start_x, start_y = world["start"]
    header[28] = start_map
    header[29] = start_x
    header[30] = start_y
    header[31] = 0

    body = bytearray()
    for m in maps:
        body.append(m.w)
        body.append(m.h)
        body.extend(b"\x00\x00")
        body.extend(struct.pack("<8H", *m.ground))
        name = m.name.encode("utf-8")
        body.extend(name + b"\x00" * (NAME_LEN - len(name)))
    assert len(body) == MAP_ENTRY_SIZE * len(maps)

    for d in doors:
        body.extend(struct.pack("<HBBBB", d["tile"], d["targetMap"], 0, d["tx"], d["ty"]))
    for n in npcs:
        body.extend(struct.pack("<HB", n["tile"], n["npcId"]))
    for m in maps:
        body.extend(struct.pack("<%dH" % len(m.cells), *m.cells))

    pack = bytes(header) + bytes(body)
    assert len(pack) == off_cells + 2 * CELL_TOTAL
    return pack


# ------------------------------------------------------------------------ main


def main(argv=None) -> int:
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description="Extract GMUD maps and tiles")
    ap.add_argument("--src", default=os.path.join(here, "ref", "gmud", "src"))
    ap.add_argument("--out", default=os.path.join(here, "data"))
    ap.add_argument("--no-preview", action="store_true", help="skip per-map PNG previews")
    args = ap.parse_args(argv)

    map_path = os.path.join(args.src, "data", "map", "gmud.map")
    img_path = os.path.join(args.src, "data", "img", "gmud_img.bin")
    for p in (map_path, img_path):
        if not os.path.isfile(p):
            print("missing input: %s" % p, file=sys.stderr)
            print("place the game's assembly sources under ref/gmud/src first", file=sys.stderr)
            return 2

    map_bytes = open(map_path, "rb").read()
    tiles = open(img_path, "rb").read()
    if len(tiles) != TILES_BIN_BYTES:
        print("gmud_img.bin is %d B, expected %d" % (len(tiles), TILES_BIN_BYTES), file=sys.stderr)
        return 2

    digest_map, digest_img = sha256(map_bytes), sha256(tiles)
    if digest_map != SHA256_MAP or digest_img != SHA256_IMG:
        print("warning: sample fingerprints differ from the known-good build", file=sys.stderr)
        print("  gmud.map     %s" % digest_map, file=sys.stderr)
        print("  gmud_img.bin %s" % digest_img, file=sys.stderr)

    world = parse_world(map_path)
    stats = validate(world)

    idh = read_utf8(os.path.join(args.src, "h", "id.h"))
    items = parse_goods_table(args.src, idh)
    kfs, pfs = parse_skill_tables(args.src, idh)
    npcs = parse_npc_table(args.src, idh, items, kfs)
    items_bin = build_items_bin(items)
    npcs_bin = build_npcs_bin(npcs)
    skills_bin = build_skills_bin(kfs, pfs)
    player_frames, player_masks = parse_player_sprites(args.src)
    player_bin = build_player_bin(player_frames, player_masks)
    npc_sprites = parse_npc_sprites(args.src)
    npcspr_bin = build_npcspr_bin(npc_sprites)
    fight_text = parse_fight_text(args.src)
    result_text = parse_result_text(args.src)
    perform_text = parse_perform_text(args.src)
    fighttext_bin, fighttext_strings = build_fighttext_bin(
        fight_text, result_text, perform_text)

    # M4 combat spot checks: 流氓 (id 21) and 捕快 (id 3).
    by_id = {n["id"]: n for n in npcs}
    liumang = by_id[21]
    if liumang["name"] != "流氓" or liumang["kfnum"] != 3 or liumang["armor"] != 5:
        raise ValueError("liumang combat data mismatch: %r" % (liumang,))
    if liumang["kf"][1] != [1, 20] or liumang["kf"][2] != [7, 30]:
        raise ValueError("liumang kf table mismatch: %r" % (liumang["kf"],))
    if by_id[3]["damage"] != 25:
        raise ValueError("bukuai longsword damage %r" % (by_id[3]["damage"],))

    os.makedirs(args.out, exist_ok=True)
    pack = build_pack(world)
    open(os.path.join(args.out, "maps.bin"), "wb").write(pack)
    open(os.path.join(args.out, "tiles.bin"), "wb").write(tiles)
    open(os.path.join(args.out, "items.bin"), "wb").write(items_bin)
    open(os.path.join(args.out, "npcs.bin"), "wb").write(npcs_bin)
    open(os.path.join(args.out, "skills.bin"), "wb").write(skills_bin)
    open(os.path.join(args.out, "player.bin"), "wb").write(player_bin)
    open(os.path.join(args.out, "npcspr.bin"), "wb").write(npcspr_bin)
    open(os.path.join(args.out, "fighttext.bin"), "wb").write(fighttext_bin)
    # charset aid for font_build.py: every codepoint of every packed entry
    # (limbs, templates, kungfu, string2.s damage/status and perform.txt).
    ft_chars = sorted(set("".join(fighttext_strings)))
    with open(os.path.join(args.out, "fighttext_chars.txt"), "w",
              encoding="utf-8") as f:
        f.write("".join(ft_chars))

    with open(os.path.join(args.out, "items.json"), "w", encoding="utf-8") as f:
        json.dump(items, f, ensure_ascii=False, indent=1)
    with open(os.path.join(args.out, "npcs.json"), "w", encoding="utf-8") as f:
        json.dump(npcs, f, ensure_ascii=False, indent=1)
    with open(os.path.join(args.out, "skills.json"), "w", encoding="utf-8") as f:
        json.dump({"kungfu": kfs, "perform": pfs}, f, ensure_ascii=False, indent=1)

    write_tile_sheet(os.path.join(args.out, "tiles.png"), tiles, masked=False, alpha=False)
    write_tile_sheet(os.path.join(args.out, "tiles_obj.png"), tiles, masked=True, alpha=True)
    write_player_sheet(os.path.join(args.out, "player.png"), player_frames, player_masks)
    write_npcspr_sheet(os.path.join(args.out, "npcspr.png"), npc_sprites)

    if not args.no_preview:
        prev_dir = os.path.join(args.out, "preview")
        os.makedirs(prev_dir, exist_ok=True)
        for m in world["maps"]:
            write_png(os.path.join(prev_dir, "map%02d.png" % m.index),
                      m.w * 32, m.h * 32,
                      compose_map(m, tiles, world["doors"], world["npcs"]), alpha=False)

    dump = {
        "format": "gmud-3ds maps v1",
        "sample": {"gmud.map": digest_map, "gmud_img.bin": digest_img},
        "start": {"map": world["start"][0], "x": world["start"][1], "y": world["start"][2]},
        "counts": {
            "maps": len(world["maps"]),
            "doors": len(world["doors"]),
            "npcs": len(world["npcs"]),
            "cells": sum(m.w * m.h for m in world["maps"]),
            "referencedDoors": stats["referencedDoors"],
            "referencedNpcs": stats["referencedNpcs"],
            "unusedDoors": stats["unusedDoors"],
        },
        "maps": [
            {
                "index": m.index, "w": m.w, "h": m.h, "name": m.name,
                "ground": list(m.ground), "cells": list(m.cells),
            }
            for m in world["maps"]
        ],
        "doors": world["doors"],
        "npcs": world["npcs"],
    }
    with open(os.path.join(args.out, "maps.json"), "w", encoding="utf-8") as f:
        json.dump(dump, f, ensure_ascii=False, indent=1)

    print("maps=%d doors=%d (referenced %d, unused %s) npcs=%d (referenced %d) cells=%d"
          % (len(world["maps"]), len(world["doors"]), stats["referencedDoors"],
             stats["unusedDoors"], len(world["npcs"]), stats["referencedNpcs"], CELL_TOTAL))
    merchants = [n for n in npcs if n["pai"] == 0xFF]
    print("maps.bin %d B, tiles.bin %d B, items.bin %d B (%d items), npcs.bin %d B (%d npcs, %d merchants)"
          % (len(pack), len(tiles), len(items_bin), len(items), len(npcs_bin), len(npcs), len(merchants)))
    print("skills.bin %d B (%d kungfu, %d performs)"
          % (len(skills_bin), len(kfs), len(pfs)))
    print("player.bin %d B (%d walk frames, 32x48 1bpp + AND masks)"
          % (len(player_bin), PLAYER_FRAME_COUNT))
    npc_named = sum(1 for w, h, _ in npc_sprites if w)
    kf_moves = sum(len(v) for v in fight_text["kf"].values())
    print("npcspr.bin %d B (%d/128 npcId auxiliary images)"
          % (len(npcspr_bin), npc_named))
    print("fighttext.bin v5 %d B (%d entries: %d limbs+templates, %d kungfu, "
          "%d move descriptions, 6x8 damage + 2x11 status + %d perform.txt labels)"
          % (len(fighttext_bin), len(fighttext_strings),
             16 + sum(len(v) for v in fight_text["tables"].values()),
             len(fight_text["kf"]), kf_moves, len(perform_text["texts"])))
    for n in merchants:
        goods = ", ".join("%s(%d)" % (items[v]["name"], items[v]["price"]) for v in n["vendor"]) or "-"
        print("  shop %-10s npcId=%3d %-8s money=%-6d goods: %s"
              % (n["label"], n["id"], n["name"], n["money"], goods))
    print("map[0]=%s %dx%d  map[3]=%s  map[38]=%s  map[46]=%s"
          % (world["maps"][0].name, world["maps"][0].w, world["maps"][0].h,
             world["maps"][3].name, world["maps"][38].name, world["maps"][46].name))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
