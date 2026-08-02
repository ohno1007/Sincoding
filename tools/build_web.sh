#!/usr/bin/env bash
# build_web.sh — 把一个 Sincoding 程序编成 Web(wasm) 成品
#
#   tools/build_web.sh <input.sin> <out_dir>
#
# 流程：sinc 转 C → emcmake cmake（emscripten 工具链）→ index.html/.js/.wasm。
# 需要：已构建的 sinc、emscripten（emcmake）、raylib 的 web 静态库。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SINC="$ROOT/compiler/build/sinc"
RAYLIB_WEB_LIB="${RAYLIB_WEB_LIB:-/usr/local/lib/web/libraylib.a}"
RAYLIB_WEB_INCLUDE="${RAYLIB_WEB_INCLUDE:-/usr/local/include}"

if [[ $# -lt 2 ]]; then echo "用法: $0 <input.sin> <out_dir> [assets_dir]" >&2; exit 2; fi
SRC="$1"; OUTDIR="$2"; ASSETS="${3:-}"

[[ -x "$SINC" ]] || { echo "找不到 sinc，请先构建 compiler" >&2; exit 1; }
command -v emcmake >/dev/null 2>&1 || { echo "需要 emscripten（emcmake 不在 PATH）" >&2; exit 1; }
[[ -f "$RAYLIB_WEB_LIB" ]] || { echo "缺少 raylib web 静态库: $RAYLIB_WEB_LIB" >&2; exit 1; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
GEN="$TMP/program.c"
BUILD="$TMP/build"

echo "[1/3] 转译 $SRC → C"
"$SINC" "$SRC" -o "$GEN"

echo "[2/3] emcmake 配置 + 构建（wasm）"
ASSETS_ARG=()
if [[ -n "$ASSETS" && -d "$ASSETS" ]]; then ASSETS_ARG=(-DSIN_ASSETS_DIR="$ASSETS"); fi
emcmake cmake -S "$ROOT/templates/web" -B "$BUILD" \
    -DSIN_PROGRAM_C="$GEN" \
    -DSIN_RUNTIME_DIR="$ROOT/runtime" \
    -DRAYLIB_WEB_LIB="$RAYLIB_WEB_LIB" \
    -DRAYLIB_WEB_INCLUDE="$RAYLIB_WEB_INCLUDE" \
    "${ASSETS_ARG[@]}" >/dev/null
cmake --build "$BUILD" >/dev/null

echo "[3/3] 收集产物 → $OUTDIR"
mkdir -p "$OUTDIR"
cp "$BUILD"/index.html "$BUILD"/index.js "$BUILD"/index.wasm "$OUTDIR"/
# 预载资源时 emscripten 还会产出 index.data，一并收集
[[ -f "$BUILD/index.data" ]] && cp "$BUILD/index.data" "$OUTDIR"/ || true
echo "完成: $OUTDIR/index.html （用 HTTP 服务器打开，wasm 不支持 file://）"
