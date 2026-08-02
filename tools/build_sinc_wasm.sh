#!/usr/bin/env bash
# build_sinc_wasm.sh — 把 Sincoding 编译器编成 WebAssembly 给浏览器用
#
# 产出 editor/sinc.js + editor/sinc.wasm（MODULARIZE 模块，导出 sin_to_blocks）。
# 编辑器据此在浏览器内做「文本 → 积木」反向同步（规范引擎，非 JS 重写）。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
command -v emcc >/dev/null 2>&1 || { echo "需要 emscripten (emcc)" >&2; exit 1; }

OUT="$ROOT/editor/sinc.js"

emcc -std=c++17 -O2 \
    -I"$ROOT/compiler/include" \
    "$ROOT/compiler/src/lexer.cpp" \
    "$ROOT/compiler/src/parser.cpp" \
    "$ROOT/compiler/src/type_checker.cpp" \
    "$ROOT/compiler/src/codegen.cpp" \
    "$ROOT/compiler/src/serializer.cpp" \
    "$ROOT/compiler/src/query.cpp" \
    "$ROOT/compiler/src/wasm_api.cpp" \
    -sMODULARIZE=1 -sEXPORT_NAME=SincModule \
    -sEXPORTED_FUNCTIONS='["_sin_to_blocks","_sin_hover","_sin_references","_sin_rename","_malloc","_free"]' \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
    -sALLOW_MEMORY_GROWTH=1 \
    -o "$OUT"

echo "完成: $OUT (+ editor/sinc.wasm)"
