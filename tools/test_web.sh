#!/usr/bin/env bash
# test_web.sh — 端到端验证 Web(wasm) 成品：构建 → 本地 HTTP 服务 → Chromium 渲染截图
# 通过则退出 0。需要 emscripten + node/playwright + python3。
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
PORT="${SIN_WEB_PORT:-8731}"
SRV=""
cleanup() { [[ -n "$SRV" ]] && kill "$SRV" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT

if ! "$ROOT/tools/build_web.sh" "$ROOT/examples/game.sin" "$WORK/web" >"$WORK/build.log" 2>&1; then
    echo "web 构建失败"; tail -5 "$WORK/build.log"; exit 1
fi

( cd "$WORK/web" && python3 -m http.server "$PORT" --bind 127.0.0.1 >/dev/null 2>&1 ) &
SRV=$!
sleep 1

out="$(NODE_PATH="$(npm root -g)" node "$ROOT/tools/verify_web.js" \
        "http://127.0.0.1:$PORT/index.html" "$WORK/shot.png" 2>&1)" || {
    echo "wasm 运行失败: $out"; exit 1; }

nonbg="$(python3 "$ROOT/tools/png_nonbg.py" "$WORK/shot.png" 2>/dev/null || echo 0)"
echo "$out · nonbg=$nonbg"
echo "$out" | grep -q '"errors":\[\]' || { echo "存在 JS 报错"; exit 1; }
[[ "$nonbg" -gt 100 ]] || { echo "画面疑似空白"; exit 1; }
exit 0
