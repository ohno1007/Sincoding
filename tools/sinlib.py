#!/usr/bin/env python3
# sinlib.py — .sinlib 库包工具：把 .sin 模块打包成单文件 JSON 库包 / 解包回 .sin
#
# 打包:  tools/sinlib.py pack -n 包名 [-d 描述] -o out.sinlib mod1.sin [mod2.sin ...]
#        模块名 = 文件名去掉 .sin（mod1.sin → import "mod1"）；
#        用 路径=名字 语法自定义：tools/sinlib.py pack ... vec.sin=mylib/vec
# 解包:  tools/sinlib.py unpack pkg.sinlib [-o 目录]
#        解出 <模块名>.sin，配合 SINCODING_PATH 或放在源码同目录即可 import。
#
# 库包是纯 JSON（非 zip）：编辑器 file:// 下可直接解析、diff 友好、无二进制依赖。
import argparse
import json
import os
import sys


def pack(args):
    modules = []
    for spec in args.modules:
        if "=" in spec:
            path, name = spec.split("=", 1)
        else:
            path = spec
            name = os.path.splitext(os.path.basename(spec))[0]
        with open(path, encoding="utf-8") as f:
            modules.append({"name": name, "src": f.read()})
    pkg = {"format": "sinlib", "version": 1, "name": args.name,
           "desc": args.desc or "", "modules": modules}
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(pkg, f, ensure_ascii=False, indent=1)
    print(f"已打包 {len(modules)} 个模块 → {args.out}")


def unpack(args):
    with open(args.pkg, encoding="utf-8") as f:
        pkg = json.load(f)
    if pkg.get("format") != "sinlib":
        sys.exit("不是有效的 .sinlib 文件")
    outdir = args.out or "."
    for m in pkg.get("modules", []):
        name = m.get("name", "")
        if not name or ".." in name or name.startswith("/"):
            continue
        path = os.path.join(outdir, name + ".sin")
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(m.get("src", ""))
        print(f"  {path}")
    print(f"已解包（import 需能找到这些文件：源码同目录或 SINCODING_PATH）")


def main():
    ap = argparse.ArgumentParser(description=".sinlib 库包工具")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack", help="打包 .sin 模块为 .sinlib")
    p.add_argument("-n", "--name", required=True, help="库包名")
    p.add_argument("-d", "--desc", default="", help="一句话描述")
    p.add_argument("-o", "--out", required=True, help="输出 .sinlib 路径")
    p.add_argument("modules", nargs="+", help="模块文件（可用 路径=模块名 自定义名字）")
    p.set_defaults(fn=pack)
    u = sub.add_parser("unpack", help="解包 .sinlib 为 .sin 文件")
    u.add_argument("pkg", help=".sinlib 文件")
    u.add_argument("-o", "--out", default=".", help="输出目录")
    u.set_defaults(fn=unpack)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
