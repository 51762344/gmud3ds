#!/usr/bin/env python3
"""图标美术设计：原版主角精灵 + 原味绿 4 级色阶，分层的像素矩阵。

A 方案「全身·窗棂」：48x48 大图，主角 1:1 全身，浅绿 LCD 底 + 点阵光晕，
双层绿框 + 四角纹样。24x24 小图为原版像素头部特写，同一套边框。

所有颜色都取自 source/gfx.c 的 s_themes[0]（原味绿）与 UI 外壳色，
不引入任何游戏外的颜色。返回的是 RGB hex 字符串矩阵，透明用 None。
"""

from __future__ import annotations

import os
import re

# s_themes[0]: shade0 paper .. shade3 ink；外壳是 s_ui_pal 里的深墨
S0, S1, S2, S3, S4 = '#aec27e', '#8fa968', '#5c7a3e', '#22300f', '#1a2412'
PALETTE = [None, S0, S1, S2, S3, S4]   # 索引 0 保留给透明，供 PixelLoom 使用

FRAME_W, FRAME_H = 32, 48
IMAGE_LABEL, MASK_LABEL = 'img_player_1', 'img_mask1'


def parse_player_s(src_dir: str):
    """读取原版 src/player.s 的全部 db 段。"""
    label_re = re.compile(r'^([A-Za-z_]\w*):\s*$')
    db_re = re.compile(r'^\s*db\s+(.*)$')
    val_re = re.compile(r'([0-9A-Fa-f]+)h\b')
    sections: dict[str, bytearray] = {}
    cur = None
    with open(os.path.join(src_dir, 'player.s'), 'r', encoding='utf-8') as f:
        for line in f:
            m = label_re.match(line)
            if m:
                cur = m.group(1)
                sections[cur] = bytearray()
                continue
            m = db_re.match(line)
            if m and cur is not None:
                vals = val_re.findall(m.group(1))
                if not vals:
                    raise ValueError('player.s: db line without bytes: %r' % line)
                sections[cur].extend(int(v, 16) for v in vals)
    for label in (IMAGE_LABEL, MASK_LABEL):
        if label not in sections or len(sections[label]) != FRAME_W * FRAME_H // 8:
            raise ValueError('player.s: missing/invalid %s' % label)
    return sections


def frame_states(sections) -> list[list[int]]:
    """正面行走帧 0 → 32x48 的 0=透明 / 1=墨 / 2=纸 矩阵。"""
    img, msk = sections[IMAGE_LABEL], sections[MASK_LABEL]
    grid = []
    for y in range(FRAME_H):
        row = []
        for x in range(FRAME_W):
            off = y * (FRAME_W // 8) + x // 8
            shift = 7 - (x & 7)
            ink = (img[off] >> shift) & 1
            keep = (msk[off] >> shift) & 1
            row.append(1 if ink else (2 if not keep else 0))
        grid.append(row)
    return grid


class Cv:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.p = [[None] * w for _ in range(h)]

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = c

    def rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, c)

    def frame(self, x, y, w, h, c, t=1):
        self.rect(x, y, w, t, c); self.rect(x, y + h - t, w, t, c)
        self.rect(x, y, t, h, c); self.rect(x + w - t, y, t, h, c)

    def line(self, x0, y0, x1, y1, c):
        dx, dy = abs(x1 - x0), abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx - dy
        while True:
            self.set(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 > -dy: err -= dy; x0 += sx
            if e2 < dx: err += dx; y0 += sy

    def sprite(self, states, sx, sy, dx, dy, w=FRAME_W, h=FRAME_H, transparent=None):
        """原版精灵 1:1 贴入，不缩放、不重绘。"""
        for y in range(h):
            for x in range(w):
                v = states[sy + y][sx + x]
                if v == 1:
                    self.set(dx + x, dy + y, S3)
                elif v == 2:
                    self.set(dx + x, dy + y, S0)
                elif transparent:
                    self.set(dx + x, dy + y, transparent)


def _lcd(cv, x, y, w, h, step=5):
    """浅绿 LCD 底 + 稀疏点阵栅格"""
    cv.rect(x, y, w, h, S0)
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if (xx * 3 + yy * 7) % step == 0:
                cv.set(xx, yy, S1)


def _corner(cv, x, y, sx, sy, c):
    cv.rect(x, y, 3, 1, c)
    cv.rect(x, y, 1, 3, c)
    cv.set(x + 1, y + 1, c)


def _leaf(cv, x, y, dx, dy):
    cv.line(x, y, x + dx, y + dy, S2)
    cv.set(x + dx, y + dy, S1)


def _frame_48(cv):
    """48x48 双层绿框 + 四角纹样（最上层）"""
    cv.frame(0, 0, 48, 48, S4)
    cv.frame(1, 1, 46, 46, S2)
    cv.frame(2, 2, 44, 44, S3)
    for x, y in ((1, 1), (44, 1), (1, 44), (44, 44)):
        _corner(cv, x, y, 1, 1, S1)
    cv.rect(3, 44, 42, 1, S2)


def _frame_24(cv):
    cv.frame(0, 0, 24, 24, S4)
    cv.frame(1, 1, 22, 22, S2)
    for x, y in ((1, 1), (21, 1), (1, 21), (21, 21)):
        _corner(cv, x, y, 1, 1, S1)


def big_layers(states):
    """48x48 大图，返回 (bg, hero, frame) 三个透明矩阵。"""
    bg = Cv(48, 48)
    _lcd(bg, 3, 3, 42, 42)
    # 主角背后的稀疏光晕（同色系，保持 LCD 质感）
    for y in range(6, 42):
        for x in range(10, 38):
            d = ((x - 24) / 12) ** 2 + ((y - 24) / 17) ** 2
            if d > 1.15 and (x * 5 + y * 3) % 7 == 0:
                bg.set(x, y, S1)
    # 左右 8px 柱：砖纹 + 竹叶（原版地图里就有的元素）
    for y in range(5, 43, 5):
        bg.rect(4, y, 3, 1, S2); bg.rect(41, y + 2, 3, 1, S2)
        bg.set(6, y + 1, S1); bg.set(41, y + 3, S1)
    for x, y, dx, dy in ((4, 12, 3, -3), (4, 30, 3, 3), (43, 16, -3, -3), (43, 34, -3, 3)):
        _leaf(bg, x, y, dx, dy)

    hero = Cv(48, 48)
    hero.sprite(states, 0, 0, 8, 3, 32, 42)   # 保留头身，脚部两行让给底边

    frame = Cv(48, 48)
    _frame_48(frame)
    return bg.p, hero.p, frame.p


def small_layers(states):
    """24x24 小图：原版像素头部特写 1:1 + 同款边框。"""
    bg = Cv(24, 24)
    _lcd(bg, 2, 2, 20, 20, 6)
    hero = Cv(24, 24)
    # x 6..25 / y 5..24：让刺猬头与脸都落在画面中下部
    hero.sprite(states, 6, 5, 2, 2, 20, 20)
    frame = Cv(24, 24)
    _frame_24(frame)
    return bg.p, hero.p, frame.p


def flatten(layers, bg=S0):
    """按顺序合成透明层，得到不带透明的 RGB 矩阵。"""
    w, h = len(layers[0][0]), len(layers[0])
    out = [[bg] * w for _ in range(h)]
    for layer in layers:
        for y in range(h):
            for x in range(w):
                c = layer[y][x]
                if c is not None:
                    out[y][x] = c
    return out
