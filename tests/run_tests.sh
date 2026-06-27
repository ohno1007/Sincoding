#!/usr/bin/env bash
# run_tests.sh — 端到端测试：Sincoding 源码 → C → 可执行 → 比对输出
#
# 验证整条转译流水线：sinc 生成 C，gcc 编译，运行后输出与期望一致。
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SINC="$ROOT/compiler/build/sinc"
WORK="$(mktemp -d)"
CC="${CC:-gcc}"
PASS=0
FAIL=0

trap 'rm -rf "$WORK"' EXIT

if [[ ! -x "$SINC" ]]; then
    echo "找不到编译器: $SINC"
    echo "请先构建: cmake -S compiler -B compiler/build && cmake --build compiler/build"
    exit 1
fi

# run_ok <name> <source.sin> <expected-output>
run_ok() {
    local name="$1" src="$2" expected="$3"
    local cfile="$WORK/$name.c" bin="$WORK/$name"
    if ! "$SINC" "$src" -o "$cfile" >/dev/null 2>"$WORK/$name.err"; then
        echo "✗ $name: 转译失败"; cat "$WORK/$name.err"; ((FAIL++)); return
    fi
    if ! "$CC" -std=c11 -O2 "$cfile" -o "$bin" 2>"$WORK/$name.cc.err"; then
        echo "✗ $name: C 编译失败"; cat "$WORK/$name.cc.err"; ((FAIL++)); return
    fi
    local got
    got="$("$bin")"
    if [[ "$got" == "$expected" ]]; then
        echo "✓ $name"; ((PASS++))
    else
        echo "✗ $name: 输出不符"
        echo "  期望: $(echo "$expected" | tr '\n' '|')"
        echo "  实际: $(echo "$got" | tr '\n' '|')"
        ((FAIL++))
    fi
}

# expect_error <name> <source.sin>  —  期望转译阶段报错（非 0 退出）
expect_error() {
    local name="$1" src="$2"
    if "$SINC" "$src" -o "$WORK/$name.c" >/dev/null 2>"$WORK/$name.err"; then
        echo "✗ $name: 本应报错却通过了"; ((FAIL++))
    else
        echo "✓ $name (正确拒绝)"; ((PASS++))
    fi
}

echo "=== 正例：转译 → 编译 → 运行 ==="
run_ok hello "$ROOT/examples/hello.sin" "42"
run_ok fib   "$ROOT/examples/fib.sin"   $'55\n55'
run_ok types "$ROOT/examples/types.sin" $'12.5664\nfalse\ntrue'

echo
echo "=== 反例：类型/语义错误应被拒绝 ==="
expect_error type_mismatch "$ROOT/tests/cases/type_mismatch.sin"
expect_error undefined_var "$ROOT/tests/cases/undefined_var.sin"
expect_error bad_cond      "$ROOT/tests/cases/bad_cond.sin"
expect_error no_main       "$ROOT/tests/cases/no_main.sin"
expect_error arg_count     "$ROOT/tests/cases/arg_count.sin"

# ---- 阶段 0：图形垂直切片（需要 raylib + xvfb，缺失则跳过） ----
have_raylib() {
    pkg-config --exists raylib 2>/dev/null && return 0
    [[ -f /usr/local/lib/libraylib.a ]]
}

echo
echo "=== 阶段 0：Sincoding → C → raylib 成品（无头渲染验证） ==="
if have_raylib && command -v xvfb-run >/dev/null 2>&1; then
    GAME="$WORK/game"
    SHOT="$WORK/shot.png"
    if "$ROOT/tools/build_native.sh" "$ROOT/examples/game.sin" "$GAME" >"$WORK/game.build.log" 2>&1; then
        # 注意：raylib 的 TakeScreenshot 只取文件名并写入当前工作目录，
        # 因此在 $WORK 内运行并用相对文件名。
        if (cd "$WORK" && LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
           SIN_MAX_FRAMES=8 SIN_SCREENSHOT="shot.png" \
           xvfb-run -a -s "-screen 0 800x600x24" "$GAME" >/dev/null 2>&1); then
            nonbg="$(python3 "$ROOT/tools/png_nonbg.py" "$SHOT" 2>/dev/null || echo 0)"
            if [[ "$nonbg" -gt 100 ]]; then
                echo "✓ game: 窗口运行并渲染角色（截图非背景像素 $nonbg）"; ((PASS++))
            else
                echo "✗ game: 截图疑似空白（非背景像素 $nonbg）"; ((FAIL++))
            fi
        else
            echo "✗ game: 无头运行失败"; ((FAIL++))
        fi
    else
        echo "✗ game: 原生构建失败"; cat "$WORK/game.build.log"; ((FAIL++))
    fi
else
    echo "○ 跳过（未检测到 raylib 或 xvfb）"
fi

echo
echo "通过 $PASS，失败 $FAIL"
[[ $FAIL -eq 0 ]]
