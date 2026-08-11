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

DEBUG=0
ARGS=()
for a in "$@"; do
    if [[ "$a" == "--debug" ]]; then DEBUG=1; else ARGS+=("$a"); fi
done
if [[ ${#ARGS[@]} -ne 2 ]]; then
    echo "用法: $0 [--debug] <input.sin> <output_bin>" >&2
    echo "  --debug  编出带 F12 调试面板的成品（imgui overlay；发布构建零开销）" >&2
    exit 2
fi
SRC="${ARGS[0]}"
OUT="${ARGS[1]}"

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
if [[ "$DEBUG" == "1" ]]; then "$SINC" "$SRC" --debug -o "$GEN"; else "$SINC" "$SRC" -o "$GEN"; fi

if [[ "$DEBUG" == "1" ]]; then
    sin_have_imgui || { sin_hint "Dear ImGui" imgui; exit 1; }
    echo "[2/2] 编译链接（含 F12 调试面板）→ $OUT"
    # imgui 是 C++：用 g++ 链接；程序与 runtime 仍按 C 编译
    IMGUI_SRC=("$SIN_IMGUI_DIR"/imgui.cpp "$SIN_IMGUI_DIR"/imgui_draw.cpp
               "$SIN_IMGUI_DIR"/imgui_tables.cpp "$SIN_IMGUI_DIR"/imgui_widgets.cpp
               "$SIN_RLIMGUI_DIR"/rlImGui.cpp "$ROOT/runtime/debug_overlay.cpp")
    $CC -std=c11 -O2 -DSIN_DEBUG -c "$GEN" -I"$ROOT/runtime" $RAYLIB_CFLAGS -o "$TMP/program.o"
    $CC -std=c11 -O2 -DSIN_DEBUG -c "$ROOT/runtime/prelude.c" -I"$ROOT/runtime" $RAYLIB_CFLAGS -o "$TMP/prelude.o"
    $CC -std=c11 -O2 -DSIN_DEBUG -c "$ROOT/runtime/runtime.c" -I"$ROOT/runtime" $RAYLIB_CFLAGS -o "$TMP/runtime.o"
    g++ -std=c++17 -O2 -DSIN_DEBUG \
        "$TMP/program.o" "$TMP/prelude.o" "$TMP/runtime.o" "${IMGUI_SRC[@]}" \
        -I"$ROOT/runtime" -I"$SIN_IMGUI_DIR" -I"$SIN_RLIMGUI_DIR" $RAYLIB_CFLAGS \
        $RAYLIB_LIBS -o "$OUT"
else
    echo "[2/2] 编译链接 → $OUT"
    $CC -std=c11 -O2 \
        "$GEN" "$ROOT/runtime/prelude.c" "$ROOT/runtime/runtime.c" \
        -I"$ROOT/runtime" $RAYLIB_CFLAGS \
        $RAYLIB_LIBS \
        -o "$OUT"
fi

echo "完成: $OUT"
