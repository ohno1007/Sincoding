#!/usr/bin/env bash
# build_sinc_wasm.sh — 把 Sincoding 编译器编成 WebAssembly 给浏览器用
#
# 产出 editor/sinc.js + editor/sinc.wasm（MODULARIZE 模块，导出 sin_to_blocks）。
# 编辑器据此在浏览器内做「文本 → 积木」反向同步（规范引擎，非 JS 重写）。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/tools/toolchains.sh"      # 自动激活 emsdk（无需用户 source emsdk_env.sh）
sin_activate_emsdk || { sin_hint emscripten web; exit 1; }

OUT="$ROOT/editor/sinc.js"
GEN="$ROOT/compiler/generated"
"$ROOT/tools/gen_std_modules.sh" "$GEN/std_modules.h" >/dev/null   # 内置标准库

emcc -std=c++17 -O2 \
    -I"$ROOT/compiler/include" -I"$GEN" \
    "$ROOT/compiler/src/lexer.cpp" \
    "$ROOT/compiler/src/parser.cpp" \
    "$ROOT/compiler/src/type_checker.cpp" \
    "$ROOT/compiler/src/codegen.cpp" \
    "$ROOT/compiler/src/serializer.cpp" \
    "$ROOT/compiler/src/query.cpp" \
    "$ROOT/compiler/src/modules.cpp" \
    "$ROOT/compiler/src/generics.cpp" \
    "$ROOT/compiler/src/blockreader.cpp" \
    "$ROOT/compiler/src/comments.cpp" \
    "$ROOT/compiler/src/wasm_api.cpp" \
    -sMODULARIZE=1 -sEXPORT_NAME=SincModule \
    -sEXPORTED_FUNCTIONS='["_sin_to_blocks","_sin_hover","_sin_references","_sin_rename","_sin_complete","_sin_runtime_decls","_sin_blocks_to_src","_malloc","_free"]' \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
    -sALLOW_MEMORY_GROWTH=1 \
    -o "$OUT"

echo "完成: $OUT (+ editor/sinc.wasm)"
