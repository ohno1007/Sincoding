#!/usr/bin/env bash
# build_native.sh — 把一个 Sincoding 程序编成原生桌面二进制
#
#   tools/build_native.sh <input.sin> <output_bin>
#
# 流程：sinc 转 C → gcc 链接 prelude/runtime → 链接 raylib → 可执行成品。
# 需要已构建编译器 (compiler/build/sinc) 与可用的 raylib。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/tools/toolchains.sh"      # 自动发现工具链（无需用户配置）
SINC="$ROOT/compiler/build/sinc"
CC="${CC:-gcc}"

if [[ $# -ne 2 ]]; then
    echo "用法: $0 <input.sin> <output_bin>" >&2
    exit 2
fi
SRC="$1"
OUT="$2"

if [[ ! -x "$SINC" ]]; then
    echo "找不到编译器 $SINC，请先: cmake -S compiler -B compiler/build && cmake --build compiler/build" >&2
    exit 1
fi

# 定位 raylib：优先 pkg-config，其次 /usr/local 里的静态库
# 注：raylib 的 .pc 常不写 Libs.private，静态链接需手动补系统依赖库。
RAYLIB_SYSLIBS="-lGL -lm -lpthread -ldl -lrt -lX11"
RAYLIB_CFLAGS=""
RAYLIB_LIBS=""
if [[ -n "${RAYLIB_LIB:-}" ]]; then          # 工具链目录 / 系统静态库（toolchains.sh 解析）
    RAYLIB_CFLAGS="-I$RAYLIB_INCLUDE"
    RAYLIB_LIBS="$RAYLIB_LIB $RAYLIB_SYSLIBS"
elif pkg-config --exists raylib 2>/dev/null; then
    RAYLIB_CFLAGS="$(pkg-config --cflags raylib)"
    RAYLIB_LIBS="$(pkg-config --libs raylib) $RAYLIB_SYSLIBS"
else
    sin_hint raylib native
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
GEN="$TMP/program.c"

echo "[1/2] 转译 $SRC → C"
"$SINC" "$SRC" -o "$GEN"

echo "[2/2] 编译链接 → $OUT"
$CC -std=c11 -O2 \
    "$GEN" "$ROOT/runtime/prelude.c" "$ROOT/runtime/runtime.c" \
    -I"$ROOT/runtime" $RAYLIB_CFLAGS \
    $RAYLIB_LIBS \
    -o "$OUT"

echo "完成: $OUT"
