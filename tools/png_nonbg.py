#!/usr/bin/env python3
# png_nonbg.py — 零依赖 PNG 解码，统计「非背景」像素数。
#
# 用于无头测试：验证 Stage 0 程序确实把角色画了出来（而非空白帧）。
#   python3 png_nonbg.py <file.png>  -> 打印非背景像素数，退出码 0
# 背景定义为接近白色 (r,g,b 均 > 230)。
import sys
import zlib
import struct


def decode_png(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "不是 PNG 文件"
    pos = 8
    width = height = bit_depth = color_type = None
    idat = b""
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
        pos += 12 + length

    assert bit_depth == 8, f"仅支持 8 位通道，得到 {bit_depth}"
    channels = {2: 3, 6: 4, 0: 1, 4: 2}[color_type]
    raw = zlib.decompress(idat)
    stride = width * channels

    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        if pa <= pb and pa <= pc:
            return a
        return b if pb <= pc else c

    out = bytearray()
    prev = bytearray(stride)
    i = 0
    for _ in range(height):
        ftype = raw[i]; i += 1
        line = bytearray(raw[i:i + stride]); i += stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if ftype == 1:
                line[x] = (line[x] + a) & 0xFF
            elif ftype == 2:
                line[x] = (line[x] + b) & 0xFF
            elif ftype == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                line[x] = (line[x] + paeth(a, b, c)) & 0xFF
        out += line
        prev = line
    return width, height, channels, out


def main():
    if len(sys.argv) != 2:
        print("用法: png_nonbg.py <file.png>", file=sys.stderr)
        return 2
    w, h, ch, px = decode_png(sys.argv[1])
    nonbg = 0
    for p in range(0, len(px), ch):
        r, g, b = px[p], px[p + 1], px[p + 2]
        if not (r > 230 and g > 230 and b > 230):
            nonbg += 1
    print(nonbg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
