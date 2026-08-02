#!/usr/bin/env bash
# render_blocks.sh — 把一个 Sincoding 程序渲染成积木视图
#
#   tools/render_blocks.sh <input.sin> [out.png]
#
# 流程：sinc --emit blocks → 写出 editor/blocks_data.js → 浏览器打开
# editor/block_viewer.html 即可看到积木；给定 out.png 则用 Chromium 截图。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SINC="$ROOT/compiler/build/sinc"

if [[ $# -lt 1 ]]; then
    echo "用法: $0 <input.sin> [out.png]" >&2
    exit 2
fi
SRC="$1"
OUT="${2:-}"

JSON="$("$SINC" "$SRC" --emit blocks)"
printf 'window.SIN_BLOCKS = %s;\n' "$JSON" > "$ROOT/editor/blocks_data.js"
echo "已写出 editor/blocks_data.js（来自 $SRC）"

if [[ -n "$OUT" ]]; then
    NODE_PATH="$(npm root -g)" node "$ROOT/tools/screenshot.js" \
        "$ROOT/editor/block_viewer.html" "$OUT"
fi
