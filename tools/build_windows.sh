#!/usr/bin/env bash
# build_windows.sh — 把一个 Sincoding 程序交叉编成 Windows .exe（MinGW-w64）
#
#   tools/build_windows.sh <input.sin> <out_exe>
#
# 流程：sinc 转 C → CMake + MinGW 工具链 → 单文件 game.exe。
# 需要：已构建的 sinc、MinGW-w64、raylib 的 Windows 静态库。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SINC="$ROOT/compiler/build/sinc"
RAYLIB_WIN_LIB="${RAYLIB_WIN_LIB:-/usr/local/lib/win/libraylib.a}"
RAYLIB_WIN_INCLUDE="${RAYLIB_WIN_INCLUDE:-/usr/local/include}"

if [[ $# -ne 2 ]]; then echo "用法: $0 <input.sin> <out_exe>" >&2; exit 2; fi
SRC="$1"; OUT="$2"

[[ -x "$SINC" ]] || { echo "找不到 sinc，请先构建 compiler" >&2; exit 1; }
command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 || { echo "需要 MinGW-w64" >&2; exit 1; }
[[ -f "$RAYLIB_WIN_LIB" ]] || { echo "缺少 raylib Windows 静态库: $RAYLIB_WIN_LIB" >&2; exit 1; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
GEN="$TMP/program.c"; BUILD="$TMP/build"

echo "[1/3] 转译 $SRC → C"
"$SINC" "$SRC" -o "$GEN"

echo "[2/3] CMake + MinGW 交叉编译"
cmake -S "$ROOT/templates/windows" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/templates/windows/mingw-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSIN_PROGRAM_C="$GEN" \
    -DSIN_RUNTIME_DIR="$ROOT/runtime" \
    -DRAYLIB_WIN_LIB="$RAYLIB_WIN_LIB" \
    -DRAYLIB_WIN_INCLUDE="$RAYLIB_WIN_INCLUDE" >/dev/null
cmake --build "$BUILD" >/dev/null

mkdir -p "$(dirname "$OUT")"
cp "$BUILD/game.exe" "$OUT"
echo "[3/3] 完成: $OUT"
file "$OUT" | sed 's/^[^:]*: //'
