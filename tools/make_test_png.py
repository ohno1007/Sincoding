#!/usr/bin/env python3
# make_test_png.py — 零依赖生成一个简单的 PNG 造型（黄色圆球，红边，透明底）
#   python3 make_test_png.py <out.png> [size]
import sys
import zlib
import struct
import math


def write_png(path, w, h, rgba):
    raw = bytearray()
    for y in range(h):
        raw.append(0)                      # 每行 filter = none
        raw += rgba[y * w * 4:(y + 1) * w * 4]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)  # 8-bit RGBA
    idat = zlib.compress(bytes(raw), 9)
    with open(path, "wb") as f:
        f.write(sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b""))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "ball.png"
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    cx = cy = n / 2.0
    r = n / 2.0 - 4
    px = bytearray(n * n * 4)
    for y in range(n):
        for x in range(n):
            d = math.hypot(x - cx + 0.5, y - cy + 0.5)
            i = (y * n + x) * 4
            if d <= r:
                px[i:i + 4] = bytes((255, 210, 26, 255))   # 黄色填充
            elif d <= r + 2:
                px[i:i + 4] = bytes((230, 70, 70, 255))     # 红色描边
            # 否则保持透明 (0,0,0,0)
    write_png(out, n, n, px)
    print("wrote", out, f"({n}x{n})")


if __name__ == "__main__":
    main()
