#!/usr/bin/env bash
# gen_std_modules.sh — 把 std/*.sin 嵌进编译器
#
#   tools/gen_std_modules.sh [输出头文件]
#
# 标准库用 Sincoding 自己写（std/*.sin），在**构建期**嵌入编译器二进制，
# 于是 import "std/xxx" 在原生 CLI 与浏览器 wasm 里都能直接用——
# 用户不需要准备任何文件、也不依赖运行时的文件系统。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/compiler/generated/std_modules.h}"
mkdir -p "$(dirname "$OUT")"

python3 - "$ROOT/std" "$OUT" <<'PY'
import sys, pathlib
std_dir, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
mods = sorted(std_dir.glob("*.sin")) if std_dir.is_dir() else []

def c_str(s: str) -> str:
    # 逐字节转义为 C 字符串（源码含中文注释，按字节走 UTF-8 最稳妥）
    out = []
    for b in s.encode("utf-8"):
        c = chr(b)
        if c == '"':   out.append('\\"')
        elif c == '\\': out.append('\\\\')
        elif c == '\n': out.append('\\n')
        elif c == '\t': out.append('\\t')
        elif c == '\r': out.append('\\r')
        elif 32 <= b < 127: out.append(c)
        else: out.append('\\%03o' % b)
    return '"' + ''.join(out) + '"'

lines = ["// 由 tools/gen_std_modules.sh 自动生成，请勿手改（源在 std/*.sin）",
         "#pragma once", "",
         "namespace sincoding {", "",
         "struct StdModule { const char* name; const char* src; };", "",
         "static const StdModule SIN_STD_MODULES[] = {"]
for m in mods:
    lines.append('    { "std/%s", %s },' % (m.stem, c_str(m.read_text(encoding="utf-8"))))
lines.append("    { nullptr, nullptr }")
lines += ["};", "", "} // namespace sincoding", ""]
out.write_text("\n".join(lines), encoding="utf-8")
print(f"嵌入 {len(mods)} 个标准库模块 → {out}")
PY
