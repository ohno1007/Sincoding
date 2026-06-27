#!/usr/bin/env bash
# build_with_bridge.sh — 把一个调用外部模块的 Sincoding 程序编成原生二进制
#
#   tools/build_with_bridge.sh <user.sin> <module.json> <out_bin>
#
# 流程：sin_bridge.py 生成 桥接(.gen.sin + _bridge.c) → 拼接 extern 声明 →
# sinc 转 C → 编译外部库（同目录 <module>.c）→ 按 static/dynamic 链接。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SINC="$ROOT/compiler/build/sinc"
CC="${CC:-gcc}"

if [[ $# -ne 3 ]]; then echo "用法: $0 <user.sin> <module.json> <out_bin>" >&2; exit 2; fi
USER_SIN="$1"; JSON="$2"; OUT="$3"
DIR="$(cd "$(dirname "$JSON")" && pwd)"
[[ -x "$SINC" ]] || { echo "找不到 sinc，请先构建 compiler" >&2; exit 1; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# 1) 生成桥接代码，读入构建变量（MODULE/LINK/LIBRARY/GEN_SIN/BRIDGE_C）
eval "$(python3 "$ROOT/tools/sin_bridge.py" "$JSON" "$TMP")"

# 2) 拼接：用户程序 + 自动生成的 extern fn 声明
cat "$USER_SIN" "$GEN_SIN" > "$TMP/combined.sin"

# 3) 转译为 C
"$SINC" "$TMP/combined.sin" -o "$TMP/program.c"

# 4) 编译外部库并链接
OUTDIR="$(cd "$(dirname "$OUT")" && pwd)"
if [[ "$LINK" == "static" ]]; then
    $CC -c "$DIR/$MODULE.c" -o "$TMP/$MODULE.o"
    $CC -std=c11 "$TMP/program.c" "$BRIDGE_C" "$TMP/$MODULE.o" -o "$OUT"
    echo "完成 (static): $OUT"
else
    $CC -shared -fPIC "$DIR/$MODULE.c" -o "$TMP/$LIBRARY"
    cp "$TMP/$LIBRARY" "$OUTDIR/"
    $CC -std=c11 "$TMP/program.c" "$BRIDGE_C" -ldl -o "$OUT"
    echo "完成 (dynamic): $OUT （运行时 LD_LIBRARY_PATH 需含 $OUTDIR）"
fi
