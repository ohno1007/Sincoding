#!/usr/bin/env python3
# make_costumes.py — 零依赖生成 guardian 小游戏的精灵造型 PNG（透明底 RGBA）。
#   python3 make_costumes.py <out_dir>
# 产出 coin.png（金币）/ bomb.png（炸弹）/ paddle.png（挡板）。
# 这些就是「造型」——游戏用 sprite_load 把它们当纹理渲染（造型即外观）。
import sys
import os
import zlib
import struct
import math


def write_png(path, w, h, px):
    raw = bytearray()
    for y in range(h):
        raw.append(0)                      # 每行 filter = none
        raw += px[y * w * 4:(y + 1) * w * 4]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)  # 8-bit RGBA
    idat = zlib.compress(bytes(raw), 9)
    with open(path, "wb") as f:
        f.write(sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b""))


def blank(n):
    return bytearray(n * n * 4)


def put(px, n, x, y, rgba):
    if 0 <= x < n and 0 <= y < n:
        i = (y * n + x) * 4
        px[i:i + 4] = bytes(rgba)


def disc(px, n, cx, cy, r, fill, edge=None, ew=2.0):
    for y in range(n):
        for x in range(n):
            d = math.hypot(x - cx + 0.5, y - cy + 0.5)
            if d <= r:
                put(px, n, x, y, fill)
            elif edge and d <= r + ew:
                put(px, n, x, y, edge)


def coin(n=64):
    px = blank(n)
    c = n / 2.0
    disc(px, n, c, c, n / 2.0 - 4, (255, 205, 30, 255), (190, 130, 0, 255), 3)
    disc(px, n, c, c, n / 2.0 - 12, (255, 230, 120, 255))     # 内圈高光
    # 中央 "★" 似的小十字，示意价值
    for d in range(-8, 9):
        put(px, n, int(c) + d, int(c), (190, 130, 0, 255))
        put(px, n, int(c), int(c) + d, (190, 130, 0, 255))
    return px


def bomb(n=64):
    px = blank(n)
    c = n / 2.0
    disc(px, n, c, c + 6, n / 2.0 - 8, (40, 40, 48, 255), (10, 10, 12, 255), 3)
    disc(px, n, c - 6, c, n / 2.0 - 20, (90, 90, 100, 255))   # 高光
    # 引信
    for t in range(14):
        put(px, n, int(c + 6 + t * 0.6), int(c - n / 2.0 + 10 - t), (120, 80, 40, 255))
    # 火花（红/黄）
    disc(px, n, c + 14, c - n / 2.0 + 8, 4, (255, 200, 40, 255))
    disc(px, n, c + 14, c - n / 2.0 + 8, 2, (255, 80, 20, 255))
    return px


def paddle(w=120, h=28):
    px = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 4
            # 圆角矩形
            corner = 10
            inx = corner <= x < w - corner or corner <= y < h - corner
            near = (min(x, w - 1 - x) ** 2 + min(y, h - 1 - y) ** 2)
            if inx or near >= corner * corner:
                top = y < h * 0.45
                px[i:i + 4] = bytes((60, 150, 235, 255) if top else (40, 110, 200, 255))
    # 描边
    for x in range(w):
        for y in (0, 1, h - 2, h - 1):
            j = (y * w + x) * 4
            if px[j + 3]:
                px[j:j + 4] = bytes((20, 60, 130, 255))
    # paddle 非方形，单独写
    return px, w, h


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "examples/assets/guardian"
    os.makedirs(out, exist_ok=True)
    write_png(os.path.join(out, "coin.png"), 48, 48, coin(48))
    write_png(os.path.join(out, "bomb.png"), 48, 48, bomb(48))
    ppx, pw, ph = paddle(112, 26)
    write_png(os.path.join(out, "paddle.png"), pw, ph, ppx)
    print("wrote coin.png / bomb.png / paddle.png ->", out)


if __name__ == "__main__":
    main()
