#!/usr/bin/env bash
# run_tests.sh — 端到端测试：Sincoding 源码 → C → 可执行 → 比对输出
#
# 验证整条转译流水线：sinc 生成 C，gcc 编译，运行后输出与期望一致。
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/tools/toolchains.sh"   # 工具链自动发现（仓库内 .toolchains/ 或系统位置）
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
    if ! "$CC" -std=c11 -O2 "$cfile" -o "$bin" -lm 2>"$WORK/$name.cc.err"; then
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
run_ok hello   "$ROOT/examples/hello.sin"   "42"
run_ok fib     "$ROOT/examples/fib.sin"     $'55\n55'
run_ok types   "$ROOT/examples/types.sin"   $'12.5664\nfalse\ntrue'
run_ok strings "$ROOT/examples/strings.sin" $'Hello, Sincoding!\n世界\ntrue\ntrue\nline1\nline2'
run_ok arrays  "$ROOT/examples/arrays.sin"  $'30\n7\n14\n0\nb'
run_ok globals_for "$ROOT/examples/globals_for.sin" $'10\n7\n10\n10'
run_ok structs "$ROOT/examples/structs.sin" $'3\n7\n14\n0\n5'
run_ok array_params "$ROOT/examples/array_params.sin" $'6\n2\n6\n1\n4'
run_ok struct_array "$ROOT/examples/struct_array.sin" $'10\n50\n50'
run_ok struct_nested "$ROOT/examples/struct_nested.sin" $'5\n9\n42\n2.5'
run_ok str_concat "$ROOT/examples/str_concat.sin" $'Score: 42\npi=3.14\nflag=true\nless'
run_ok mathx "$ROOT/examples/mathx.sin" $'5\n10\n0\n7.5'
run_ok use_std "$ROOT/examples/use_std.sin" $'5\n10\n2.5\n-1\n10\n7\n9'
run_ok use_arrayx "$ROOT/examples/use_arrayx.sin" $'15\n60\n7\n5\n3\n0\n1\n1.5\n5'
run_ok generics "$ROOT/examples/generics.sin" $'10\n7\n9\n1.5'

echo
echo "=== 反例：类型/语义错误应被拒绝 ==="
expect_error type_mismatch "$ROOT/tests/cases/type_mismatch.sin"
expect_error undefined_var "$ROOT/tests/cases/undefined_var.sin"
expect_error bad_cond      "$ROOT/tests/cases/bad_cond.sin"
expect_error no_main       "$ROOT/tests/cases/no_main.sin"
expect_error arg_count     "$ROOT/tests/cases/arg_count.sin"
expect_error string_arith  "$ROOT/tests/cases/string_arith.sin"
expect_error array_len     "$ROOT/tests/cases/array_len.sin"
expect_error for_bad       "$ROOT/tests/cases/for_bad.sin"
expect_error struct_field   "$ROOT/tests/cases/struct_field.sin"
expect_error array_param_len "$ROOT/tests/cases/array_param_len.sin"
expect_error array_ret_len   "$ROOT/tests/cases/array_ret_len.sin"
expect_error struct_array_type "$ROOT/tests/cases/struct_array_type.sin"
expect_error struct_self_ref   "$ROOT/tests/cases/struct_self_ref.sin"
expect_error struct_arr_field  "$ROOT/tests/cases/struct_arr_field.sin"
expect_error import_missing    "$ROOT/tests/cases/import_missing.sin"
expect_error slice_return      "$ROOT/tests/cases/slice_return.sin"
expect_error slice_field       "$ROOT/tests/cases/slice_field.sin"
expect_error generic_conflict  "$ROOT/tests/cases/generic_conflict.sin"
expect_error generic_uninferable "$ROOT/tests/cases/generic_uninferable.sin"

# roundtrip <name> <source.sin> — 验证 AST ⇄ 文本 ⇄ 积木 序列化正确
roundtrip() {
    local name="$1" src="$2"
    local r1="$WORK/$name.r1.sin" r2="$WORK/$name.r2.sin"
    local c0="$WORK/$name.c0" c1="$WORK/$name.c1"
    "$SINC" "$src" --emit src -o "$r1" >/dev/null 2>&1
    "$SINC" "$r1" --emit src -o "$r2" >/dev/null 2>&1
    # 1) 序列化幂等：源码 → AST → 源码 → AST → 源码，后两次一致
    if ! diff -q "$r1" "$r2" >/dev/null 2>&1; then
        echo "✗ $name: 序列化非幂等"; ((FAIL++)); return
    fi
    # 2) 语义不变：往返后生成的 C 与原始一致
    "$SINC" "$src" --emit c -o "$c0" >/dev/null 2>&1
    "$SINC" "$r1"  --emit c -o "$c1" >/dev/null 2>&1
    if ! diff -q "$c0" "$c1" >/dev/null 2>&1; then
        echo "✗ $name: 往返后 C 代码改变"; ((FAIL++)); return
    fi
    # 3) 积木模型为合法 JSON
    if ! "$SINC" "$src" --emit blocks 2>/dev/null | python3 -c "import json,sys; json.load(sys.stdin)" 2>/dev/null; then
        echo "✗ $name: 积木 JSON 非法"; ((FAIL++)); return
    fi
    echo "✓ $name (幂等 + C 等价 + 合法积木 JSON)"; ((PASS++))
}

echo
echo "=== 序列化往返：AST ⇄ 文本 / 积木（积木编辑器地基） ==="
roundtrip hello   "$ROOT/examples/hello.sin"
roundtrip fib     "$ROOT/examples/fib.sin"
roundtrip types   "$ROOT/examples/types.sin"
roundtrip game    "$ROOT/examples/game.sin"
roundtrip strings "$ROOT/examples/strings.sin"
roundtrip arrays  "$ROOT/examples/arrays.sin"
roundtrip globals_for "$ROOT/examples/globals_for.sin"
roundtrip structs "$ROOT/examples/structs.sin"
roundtrip array_params "$ROOT/examples/array_params.sin"
roundtrip struct_array "$ROOT/examples/struct_array.sin"
roundtrip struct_nested "$ROOT/examples/struct_nested.sin"
roundtrip str_concat "$ROOT/examples/str_concat.sin"
roundtrip mathx "$ROOT/examples/mathx.sin"
roundtrip use_std "$ROOT/examples/use_std.sin"
roundtrip use_arrayx "$ROOT/examples/use_arrayx.sin"
roundtrip generics "$ROOT/examples/generics.sin"

# ---- 模块系统：用户库（磁盘）/ 嵌套 import / extern 去重 / 循环检测 ----
echo
echo "=== 模块系统：import 解析 ==="
LIB="$WORK/lib"; mkdir -p "$LIB"
printf '%s\n' 'import "std/mathx"' 'struct Vec2 { x: float, y: float }' \
  'fn v_len(v: Vec2) -> float { return dist(0.0, 0.0, v.x, v.y) }' > "$LIB/geom.sin"
printf '%s\n' 'import "geom"' 'extern fn sqrt(x: float) -> float' 'fn main() -> int {' \
  '    let v = Vec2 { x: 3.0, y: 4.0 }' '    print(v_len(v))' '    print(sqrt(9.0))' \
  '    return 0' '}' > "$LIB/app.sin"
if "$SINC" "$LIB/app.sin" -o "$WORK/app.c" >/dev/null 2>&1 && \
   "$CC" -std=c11 -O2 "$WORK/app.c" -o "$WORK/app" -lm 2>/dev/null && \
   [[ "$("$WORK/app" | tr '\n' ' ')" == "5 3 " ]]; then
    echo "✓ import: 磁盘用户库 + 嵌套 import(std) + extern 去重"; ((PASS++))
else echo "✗ import: 用户库解析失败"; ((FAIL++)); fi

printf '%s\n' 'import "b"' 'fn fa() -> int { return 1 }' > "$LIB/a.sin"
printf '%s\n' 'import "a"' 'fn fb() -> int { return 2 }' > "$LIB/b.sin"
printf '%s\n' 'import "a"' 'fn main() -> int { return fa() }' > "$LIB/circ.sin"
if "$SINC" "$LIB/circ.sin" -o /dev/null 2>&1 | grep -q "循环导入" || \
   ! "$SINC" "$LIB/circ.sin" -o /dev/null >/dev/null 2>&1; then
    echo "✓ import: 循环导入被检测并拒绝"; ((PASS++))
else echo "✗ import: 循环导入未被检测"; ((FAIL++)); fi

# 导入后 --emit src 只写回 import 行（库源码不得灌进用户文件）
emitted="$("$SINC" "$ROOT/examples/use_std.sin" --emit src 2>/dev/null)"
if [[ "$(grep -c '^import "std/mathx"' <<<"$emitted")" == "1" ]] && \
   [[ "$(grep -c '^fn clamp' <<<"$emitted")" == "0" ]]; then
    echo "✓ import: --emit src 只写回 import 行（库源码未污染用户文件）"; ((PASS++))
else echo "✗ import: 序列化把库源码灌进来了"; ((FAIL++)); fi

# ---- 切片 T[]：一份代码覆盖任意长度 + 借用语义 ----
SLSRC="$WORK/slice.sin"
printf '%s\n' 'fn sum(xs: int[]) -> int {' '    let t = 0' '    for i in 0..len(xs) { t = t + xs[i] }' \
  '    return t' '}' 'fn fill(xs: int[], v: int) { for i in 0..len(xs) { xs[i] = v } }' \
  'fn main() -> int {' '    let a: int[3] = [1,2,3]' '    let b: int[5] = [1,2,3,4,5]' \
  '    print(sum(a))' '    print(sum(b))' '    fill(a, 7)' '    print(sum(a))' '    return 0' '}' > "$SLSRC"
if "$SINC" "$SLSRC" -o "$WORK/slice.c" >/dev/null 2>&1 && \
   "$CC" -std=c11 -O2 "$WORK/slice.c" -o "$WORK/slice" -lm 2>/dev/null && \
   [[ "$("$WORK/slice" | tr '\n' ' ')" == "6 15 21 " ]]; then
    echo "✓ slice: 同一函数处理 int[3]/int[5] + 通过切片就地修改生效"; ((PASS++))
else echo "✗ slice: 行为不符（$("$WORK/slice" 2>/dev/null | tr '\n' ' ')）"; ((FAIL++)); fi

# ---- IDE 查询：悬停显示类型（mini-LSP）----
echo
echo "=== IDE 查询：hover 显示类型 ==="
HOVSRC="$WORK/hover.sin"
printf '%s\n' 'fn main() -> int {' '    let xs: int[5] = [1,2,3,4,5]' '    return xs[0]' '}' > "$HOVSRC"
if "$SINC" "$HOVSRC" --query hover 3 12 2>/dev/null | grep -q '"type":"int\[5\]"'; then
    echo "✓ hover: xs 引用显示 int[5]"; ((PASS++))
else
    echo "✗ hover: 类型不符（$("$SINC" "$HOVSRC" --query hover 3 12 2>/dev/null)）"; ((FAIL++))
fi

# references：两函数同名变量不互相干扰
REFSRC="$WORK/refs.sin"
printf '%s\n' 'fn f() -> int {' '    let x = 1' '    return x' '}' 'fn g() -> int {' '    let x = 2' '    return x' '}' 'fn main() -> int { return f() + g() }' > "$REFSRC"
nrefs="$("$SINC" "$REFSRC" --query refs 2 9 2>/dev/null | grep -o '"line"' | wc -l)"
if [[ "$nrefs" == "2" ]]; then
    echo "✓ refs: f 的 x 仅 2 处引用（不含 g 的 x，作用域正确）"; ((PASS++))
else echo "✗ refs: 引用数=$nrefs（期望 2）"; ((FAIL++)); fi

# rename：局部变量改名不跨函数
if "$SINC" "$REFSRC" --query rename 2 9 y 2>/dev/null | grep -q 'let y' && \
   "$SINC" "$REFSRC" --query rename 2 9 y 2>/dev/null | grep -q 'let x'; then
    echo "✓ rename: f 的 x→y，g 的 x 保留（作用域正确）"; ((PASS++))
else echo "✗ rename: 作用域错误"; ((FAIL++)); fi

# ---- 积木视图渲染（需要 node + playwright，缺失则跳过） ----
echo
echo "=== 积木视图渲染（Chromium 截图验证） ==="
if command -v node >/dev/null 2>&1 && \
   NODE_PATH="$(npm root -g 2>/dev/null)" node -e "require('playwright')" >/dev/null 2>&1; then
    SHOT="$WORK/blocks.png"
    if "$ROOT/tools/render_blocks.sh" "$ROOT/examples/fib.sin" "$SHOT" >/dev/null 2>&1; then
        nonbg="$(python3 "$ROOT/tools/png_nonbg.py" "$SHOT" 2>/dev/null || echo 0)"
        if [[ "$nonbg" -gt 1000 ]]; then
            echo "✓ blocks: 积木视图已渲染（非背景像素 $nonbg）"; ((PASS++))
        else
            echo "✗ blocks: 渲染疑似空白（非背景像素 $nonbg）"; ((FAIL++))
        fi
    else
        echo "✗ blocks: 渲染失败"; ((FAIL++))
    fi
else
    echo "○ 跳过（未检测到 node 或 playwright）"
fi

# ---- 引擎 blocks → src 往返（JS 镜像已退役，序列化只有 C++ 一份实现）----
echo
echo "=== 积木 → 文本：引擎往返一致（唯一序列化器） ==="
if command -v node >/dev/null 2>&1 && [[ -f "$ROOT/editor/sinc.js" ]]; then
    b2s_bad=0
    for ex in hello fib types game strings arrays globals_for structs array_params \
              struct_array struct_nested str_concat mathx use_std use_arrayx generics guardian; do
        "$SINC" "$ROOT/examples/$ex.sin" --emit blocks > "$WORK/$ex.bj" 2>/dev/null
        "$SINC" "$ROOT/examples/$ex.sin" --emit src   > "$WORK/$ex.want" 2>/dev/null
        node -e "
          const fs=require('fs');
          require('$ROOT/editor/sinc.js')().then(m=>{
            const r=JSON.parse(m.ccall('sin_blocks_to_src','string',['string'],
                     [fs.readFileSync('$WORK/$ex.bj','utf8')]));
            if(!r.ok){process.exit(1);}
            fs.writeFileSync('$WORK/$ex.got', r.source);
          }).catch(()=>process.exit(1));
        " >/dev/null 2>&1
        if ! diff -q "$WORK/$ex.want" "$WORK/$ex.got" >/dev/null 2>&1; then
            echo "  ✗ $ex: blocks → src 与 --emit src 不一致"; b2s_bad=$((b2s_bad+1))
        fi
    done
    if [[ "$b2s_bad" == "0" ]]; then
        echo "✓ blocks→src: 17 个例子逐字节一致（含 import/泛型/切片/嵌套结构体）"; ((PASS++))
    else
        echo "✗ blocks→src: $b2s_bad 个不一致"; ((FAIL++))
    fi
else
    echo "○ 跳过（未检测到 node 或 sinc.js）"
fi

# ---- 积木 IDE 交互（需要 node + playwright） ----
echo
echo "=== 积木 IDE 交互（写回 + 造型画板，Chromium 驱动） ==="
if command -v node >/dev/null 2>&1 && \
   NODE_PATH="$(npm root -g 2>/dev/null)" node -e "require('playwright')" >/dev/null 2>&1; then
    "$ROOT/tools/render_blocks.sh" "$ROOT/examples/fib.sin" >/dev/null 2>&1
    # 若有 emcc，刷新浏览器内 wasm 编译器（文本→积木反向同步用）
    command -v emcc >/dev/null 2>&1 && "$ROOT/tools/build_sinc_wasm.sh" >/dev/null 2>&1
    if out="$(NODE_PATH="$(npm root -g)" node "$ROOT/tools/verify_ide.js" "$ROOT/editor/index.html" "$WORK" 2>&1)"; then
        echo "✓ IDE: 字段编辑写回文本 + 造型画板可绘制（$out）"; ((PASS++))
    else
        echo "✗ IDE: 交互验证失败（$out）"; ((FAIL++))
    fi
    # mini-LSP 前端：F2 重命名（作用域正确）+ 光标处 hover 显示类型
    if rout="$(NODE_PATH="$(npm root -g)" node "$ROOT/tools/verify_rename.js" "$ROOT/editor/index.html" 2>&1)"; then
        echo "✓ IDE mini-LSP: F2 重命名 + 光标 hover 类型（$rout）"; ((PASS++))
    else
        echo "✗ IDE mini-LSP: 验证失败（$rout）"; ((FAIL++))
    fi
    # 函数签名可从积木编辑（加参数 / 切片类型 / 返回类型）
    if fout="$(NODE_PATH="$(npm root -g)" node "$ROOT/tools/verify_fn_signature.js" "$ROOT/editor/index.html" 2>&1)"; then
        echo "✓ 函数签名积木: 加参数 + 切片类型 + 改返回类型（$fout）"; ((PASS++))
    else
        echo "✗ 函数签名积木: 验证失败（$fout）"; ((FAIL++))
    fi
    # import 在积木侧不丢失 + 可从调色板可视化添加
    if iout="$(NODE_PATH="$(npm root -g)" node "$ROOT/tools/verify_import_blocks.js" "$ROOT/editor/index.html" 2>&1)"; then
        echo "✓ import 积木: 编辑积木不丢 import + 调色板可添加（$iout）"; ((PASS++))
    else
        echo "✗ import 积木: 验证失败（$iout）"; ((FAIL++))
    fi
    # 预览 = 成品：import 的库函数在预览里也能真正执行（不只是有积木）
    if pout="$(NODE_PATH="$(npm root -g)" node "$ROOT/tools/verify_lib_preview.js" "$ROOT/editor/index.html" 2>&1)"; then
        echo "✓ 预览库调用: import 的库函数在预览里执行正确（$pout）"; ((PASS++))
    else
        echo "✗ 预览库调用: 验证失败（$pout）"; ((FAIL++))
    fi
    # 积木级调试器：断点 → 暂停 → 变量/调用栈 → 单步
    if dout="$(NODE_PATH="$(npm root -g)" node "$ROOT/tools/verify_debugger.js" "$ROOT/editor/index.html" 2>&1)"; then
        echo "✓ IDE 调试器: 断点命中 + 变量面板 + 调用栈 + 单步（$dout）"; ((PASS++))
    else
        echo "✗ IDE 调试器: 验证失败（$dout）"; ((FAIL++))
    fi
    # 导入即得积木：import 库后调色板自动出现该库分类（积木由签名生成）
    if lout="$(NODE_PATH="$(npm root -g)" node "$ROOT/tools/verify_lib_palette.js" "$ROOT/editor/index.html" 2>&1)"; then
        echo "✓ IDE 库积木: import 后调色板自动出现库分类（$lout）"; ((PASS++))
    else
        echo "✗ IDE 库积木: 验证失败（$lout）"; ((FAIL++))
    fi
else
    echo "○ 跳过（未检测到 node 或 playwright）"
fi

# ---- 阶段 0：图形垂直切片（需要 raylib + xvfb，缺失则跳过） ----
have_raylib() { sin_have_native; }

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

# ---- 运行时：字符串接入（sprite_load PNG + say + draw_text） ----
echo
echo "=== 运行时：字符串接入（加载 PNG 造型 + 文字渲染） ==="
if have_raylib && command -v xvfb-run >/dev/null 2>&1; then
    SAY="$WORK/say"
    if "$ROOT/tools/build_native.sh" "$ROOT/examples/say.sin" "$SAY" >/dev/null 2>&1; then
        cp "$ROOT/examples/assets/ball.png" "$WORK/"
        if (cd "$WORK" && LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
           SIN_MAX_FRAMES=8 SIN_SCREENSHOT="say.png" \
           xvfb-run -a -s "-screen 0 800x600x24" "$SAY" >/dev/null 2>&1); then
            nonbg="$(python3 "$ROOT/tools/png_nonbg.py" "$WORK/say.png" 2>/dev/null || echo 0)"
            if [[ "$nonbg" -gt 1000 ]]; then
                echo "✓ say: 加载 PNG 造型为纹理 + say/draw_text 文字渲染（非背景像素 $nonbg）"; ((PASS++))
            else echo "✗ say: 渲染疑似空白（$nonbg）"; ((FAIL++)); fi
        else echo "✗ say: 无头运行失败"; ((FAIL++)); fi
    else echo "✗ say: 构建失败"; ((FAIL++)); fi
else
    echo "○ 跳过（未检测到 raylib 或 xvfb）"
fi

# ---- 完整小游戏：接球（数组 + 输入 + 文字 + 计分逻辑） ----
echo
echo "=== 完整小游戏「接球」（数组/字符串/输入/计分综合） ==="
if have_raylib && command -v xvfb-run >/dev/null 2>&1; then
    CATCH="$WORK/catch"
    if "$ROOT/tools/build_native.sh" "$ROOT/examples/catch.sin" "$CATCH" >/dev/null 2>&1; then
        score="$(cd "$WORK" && LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
                  SIN_MAX_FRAMES=60 SIN_SCREENSHOT="catch.png" \
                  xvfb-run -a -s "-screen 0 800x600x24" "$CATCH" 2>/dev/null | tail -1)"
        nonbg="$(python3 "$ROOT/tools/png_nonbg.py" "$WORK/catch.png" 2>/dev/null || echo 0)"
        if [[ "$score" =~ ^[0-9]+$ && "$score" -ge 1 && "$nonbg" -gt 1000 ]]; then
            echo "✓ catch: 游戏运行，计分逻辑生效（最终分数 $score，画面像素 $nonbg）"; ((PASS++))
        else
            echo "✗ catch: 分数=$score 像素=$nonbg"; ((FAIL++))
        fi
    else echo "✗ catch: 构建失败"; ((FAIL++)); fi
else
    echo "○ 跳过（未检测到 raylib 或 xvfb）"
fi

# ---- 阶段 5：JSON 桥接外部 ELF（静态 + 动态） ----
echo
echo "=== 阶段 5：JSON 桥接外部 ELF（static + dynamic dlopen） ==="
if command -v gcc >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
    bdir="$WORK/bridge"; mkdir -p "$bdir"
    if "$ROOT/tools/build_with_bridge.sh" "$ROOT/examples/bridge/use_motor.sin" \
         "$ROOT/examples/bridge/motor.json" "$bdir/m_static" >/dev/null 2>&1; then
        got="$("$bdir/m_static" 2>/dev/null)"
        if [[ "$got" == *"120"* && "$got" == *"15"* ]]; then
            echo "✓ bridge-static（链接外部库，调用 + 类型转换正确）"; ((PASS++))
        else echo "✗ bridge-static 输出异常: $(echo "$got" | tr '\n' '|')"; ((FAIL++)); fi
    else echo "✗ bridge-static 构建失败"; ((FAIL++)); fi

    if "$ROOT/tools/build_with_bridge.sh" "$ROOT/examples/bridge/use_motor.sin" \
         "$ROOT/examples/bridge/motor_dyn.json" "$bdir/m_dyn" >/dev/null 2>&1; then
        got="$(LD_LIBRARY_PATH="$bdir" "$bdir/m_dyn" 2>/dev/null)"
        if [[ "$got" == *"120"* && "$got" == *"15"* ]]; then
            echo "✓ bridge-dynamic（dlopen/dlsym 经函数指针调用）"; ((PASS++))
        else echo "✗ bridge-dynamic 输出异常: $(echo "$got" | tr '\n' '|')"; ((FAIL++)); fi
    else echo "✗ bridge-dynamic 构建失败"; ((FAIL++)); fi
else
    echo "○ 跳过（缺 gcc/python3）"
fi

# ---- 阶段 4：Web(wasm) 成品（需要 emscripten + node/playwright） ----
echo
echo "=== 阶段 4：Sincoding → wasm → 浏览器渲染（emscripten） ==="
if sin_have_web && \
   command -v node >/dev/null 2>&1 && \
   NODE_PATH="$(npm root -g 2>/dev/null)" node -e "require('playwright')" >/dev/null 2>&1; then
    chmod +x "$ROOT/tools/test_web.sh"
    if out="$("$ROOT/tools/test_web.sh" 2>&1)"; then
        echo "✓ web: wasm 在浏览器中渲染角色（$out）"; ((PASS++))
    else
        echo "✗ web: $out"; ((FAIL++))
    fi
    # 接球小游戏也编成 wasm 在浏览器渲染
    if out="$("$ROOT/tools/test_web.sh" "$ROOT/examples/catch.sin" 2>&1)"; then
        echo "✓ web-catch: 接球游戏 wasm 在浏览器运行（$out）"; ((PASS++))
    else
        echo "✗ web-catch: $out"; ((FAIL++))
    fi
    # guardian（结构体数组 + 造型 PNG）编成 wasm，验证 assets 预载打包成功。
    # 只验构建产物（不走浏览器渲染，避免 headless Chromium 对大 wasm 的偶发崩溃；
    # 浏览器渲染已由上面的 web / web-catch 覆盖）。
    gwdir="$WORK/gweb"
    if "$ROOT/tools/build_web.sh" "$ROOT/examples/guardian.sin" "$gwdir" "$ROOT/examples/assets/guardian" >/dev/null 2>&1 \
       && [[ -f "$gwdir/index.data" ]]; then
        echo "✓ web-guardian: 带造型资源编成 wasm（assets 预载 index.data $(wc -c <"$gwdir/index.data") 字节）"; ((PASS++))
    else
        echo "✗ web-guardian: 带 assets 的 web 构建失败"; ((FAIL++))
    fi
else
    echo "○ 跳过（未检测到 emscripten / raylib-web / playwright）"
fi

# ---- 阶段 4：Windows .exe（MinGW 交叉编译） ----
echo
echo "=== 阶段 4：Sincoding → Windows .exe（MinGW 交叉编译） ==="
if sin_have_windows; then
    if "$ROOT/tools/build_windows.sh" "$ROOT/examples/game.sin" "$WORK/game.exe" >/dev/null 2>&1; then
        ft="$(file "$WORK/game.exe")"
        if [[ "$ft" == *"PE32+"* && "$ft" == *"Windows"* ]]; then
            echo "✓ windows: 交叉编译出单文件 PE32+ exe"; ((PASS++))
        else echo "✗ windows: 产物非 PE32+（$ft）"; ((FAIL++)); fi
    else echo "✗ windows: 构建失败"; ((FAIL++)); fi
else
    echo "○ 跳过（未检测到 MinGW / raylib-win）"
fi

# ---- 阶段 4：Android .so（NDK 交叉编译） ----
echo
echo "=== 阶段 4：Sincoding → Android .so（NDK 交叉编译） ==="
NDK_BIN="${ANDROID_NDK:-}/toolchains/llvm/prebuilt/linux-x86_64/bin"
NDK_NM="$NDK_BIN/llvm-nm"
if sin_have_android; then
    if "$ROOT/tools/build_android.sh" \
         "$ROOT/examples/game.sin" "$WORK/libsincoding.so" >/dev/null 2>&1; then
        ft="$(file "$WORK/libsincoding.so")"
        if [[ "$ft" == *"aarch64"* ]] && \
           "$NDK_NM" -D "$WORK/libsincoding.so" 2>/dev/null | grep -q ANativeActivity_onCreate; then
            echo "✓ android: 交叉编译出 arm64 NativeActivity .so（导出 ANativeActivity_onCreate）"; ((PASS++))
        else echo "✗ android: 产物不符（$ft）"; ((FAIL++)); fi
    else echo "✗ android: 构建失败"; ((FAIL++)); fi
else
    echo "○ 跳过（未检测到 NDK / raylib-android）"
fi

# ---- 完整项目：共享状态游戏 guardian（编译 + 无头跑 + 计分逻辑） ----
echo
echo "=== 完整项目：guardian.sin（共享结构体/全局/数组 → 转 C → 跑） ==="
GUARD="$WORK/guardian"
if "$ROOT/tools/build_native.sh" "$ROOT/examples/guardian.sin" "$GUARD" >/dev/null 2>&1; then
    # 从造型 assets 目录运行，使 sprite_load("coin.png") 等相对路径可解析
    if out="$(cd "$ROOT/examples/assets/guardian" && SIN_MAX_FRAMES=120 xvfb-run -a -s "-screen 0 800x600x24" "$GUARD" 2>/dev/null | tail -1)"; then
        if [[ "$out" =~ ^[0-9]+$ ]]; then
            echo "✓ guardian: 完整游戏跑通（退出分数=$out，共享 GameState/全局数组生效）"; ((PASS++))
        else echo "✗ guardian: 输出异常（$out）"; ((FAIL++)); fi
    else echo "✗ guardian: 运行失败"; ((FAIL++)); fi
else echo "✗ guardian: 构建失败"; ((FAIL++)); fi

# ---- 画笔持久层（pen.sin 无头跑，截图应有大量非背景像素=笔迹累积） ----
echo
echo "=== 画笔：pen.sin（持久绘制层 + 平台 API random/screen） ==="
PENBIN="$WORK/pen"
if "$ROOT/tools/build_native.sh" "$ROOT/examples/pen.sin" "$PENBIN" >/dev/null 2>&1; then
    if ( cd "$WORK" && SIN_MAX_FRAMES=60 SIN_SCREENSHOT=pen.png \
         xvfb-run -a -s "-screen 0 600x600x24" "$PENBIN" >/dev/null 2>&1 ) && [[ -f "$WORK/pen.png" ]]; then
        nb="$(python3 "$ROOT/tools/png_nonbg.py" "$WORK/pen.png" 2>/dev/null || echo 0)"
        if [[ "${nb:-0}" -gt 2000 ]]; then
            echo "✓ pen: 画笔持久层累积笔迹（非背景像素 $nb）"; ((PASS++))
        else echo "✗ pen: 笔迹像素过少（$nb）"; ((FAIL++)); fi
    else echo "✗ pen: 运行/截图失败"; ((FAIL++)); fi
else echo "✗ pen: 构建失败"; ((FAIL++)); fi

# ---- 原生成品调试面板（imgui overlay，--debug 构建）----
echo
echo "=== 成品调试面板：sinc --debug + imgui overlay ==="
if sin_have_native && sin_have_imgui && command -v xvfb-run >/dev/null 2>&1 && command -v g++ >/dev/null 2>&1; then
    # 1) 发布构建不得含调试钩子（零开销）
    "$SINC" "$ROOT/examples/game.sin" -o "$WORK/rel.c" >/dev/null 2>&1
    relhooks="$(grep -c 'sin_dbg' "$WORK/rel.c" || true)"
    # 2) --debug 构建能编出成品，且面板渲染出内容
    if [[ "$relhooks" == "0" ]] && \
       "$ROOT/tools/build_native.sh" --debug "$ROOT/examples/game.sin" "$WORK/gdbg" >/dev/null 2>&1; then
        ( cd "$WORK" && LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe SIN_DBG_OPEN=1 \
          SIN_MAX_FRAMES=12 SIN_SCREENSHOT="dbg.png" \
          xvfb-run -a -s "-screen 0 800x600x24" "$WORK/gdbg" >/dev/null 2>&1 )
        nb="$(python3 "$ROOT/tools/png_nonbg.py" "$WORK/dbg.png" 2>/dev/null || echo 0)"
        if [[ "${nb:-0}" -gt 50000 ]]; then
            echo "✓ 调试面板: --debug 成品渲染出 imgui 面板（像素 $nb），发布构建无调试钩子"; ((PASS++))
        else echo "✗ 调试面板: 面板疑似未渲染（像素 $nb）"; ((FAIL++)); fi
    else echo "✗ 调试面板: 构建失败或发布构建含调试钩子（$relhooks）"; ((FAIL++)); fi
else
    echo "○ 跳过（缺 raylib / imgui / xvfb / g++）"
fi

# ---- 碰撞检测（collide.sin：sprite_touching AABB 无头验证）----
echo
echo "=== 碰撞：collide.sin（sprite_touching AABB） ==="
if have_raylib && command -v xvfb-run >/dev/null 2>&1; then
    CBIN="$WORK/collide"
    if "$ROOT/tools/build_native.sh" "$ROOT/examples/collide.sin" "$CBIN" >/dev/null 2>&1; then
        hits="$(SIN_MAX_FRAMES=60 xvfb-run -a -s "-screen 0 400x400x24" "$CBIN" 2>/dev/null | tail -1)"
        if [[ "$hits" =~ ^[0-9]+$ && "$hits" -gt 0 ]]; then
            echo "✓ collide: sprite_touching 生效（碰撞 $hits 帧）"; ((PASS++))
        else echo "✗ collide: 碰撞帧数异常（$hits）"; ((FAIL++)); fi
    else echo "✗ collide: 构建失败"; ((FAIL++)); fi
else
    echo "○ 跳过（未检测到 raylib 或 xvfb）"
fi

# ---- Android APK 打包（aapt2 链接 + 签名，需 Android SDK build-tools） ----
echo
echo "=== Android APK：guardian.sin → 签名 APK（需 SDK build-tools） ==="
APK_SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
APK_BT="$APK_SDK/build-tools/${ANDROID_BUILD_TOOLS:-34.0.0}"
if sin_have_apk && \
   [[ -x "$APK_BT/aapt2" ]] && [[ -x "$APK_BT/apksigner" ]] && \
   [[ -f "$APK_SDK/platforms/${ANDROID_PLATFORM:-android-29}/android.jar" ]]; then
    APK="$WORK/guardian.apk"
    if ANDROID_SDK_ROOT="$APK_SDK" \
         "$ROOT/tools/build_apk.sh" "$ROOT/examples/guardian.sin" "$APK" "Guardian" >/dev/null 2>&1; then
        # 解析二进制 Manifest + 校验签名 + 确认含原生库
        if "$APK_BT/aapt2" dump badging "$APK" 2>/dev/null | grep -q "native-code: 'arm64-v8a'" && \
           "$APK_BT/apksigner" verify "$APK" >/dev/null 2>&1 && \
           unzip -l "$APK" 2>/dev/null | grep -q "lib/arm64-v8a/libsincoding.so" && \
           unzip -l "$APK" 2>/dev/null | grep -q "assets/coin.png"; then
            echo "✓ apk: 打出可安装的签名 APK（NativeActivity + arm64 原生库 + 造型 assets，签名校验通过）"; ((PASS++))
        else echo "✗ apk: 产物校验未通过"; ((FAIL++)); fi
    else echo "✗ apk: 打包失败"; ((FAIL++)); fi
else
    echo "○ 跳过（未检测到 NDK / raylib-android / SDK build-tools）"
fi

# ---- 发布流水线：发布模态框 → 本地构建服务 → 产物（Chromium 驱动，linux 最快） ----
# ---- 调色板积木：每个新增积木产出的文本都能被规范引擎解析+编译 ----
echo
echo "=== 调色板积木：序列化 → sinc 解析+生成 C → gcc 编译 ==="
if command -v node >/dev/null 2>&1; then
    PB="$WORK/palette_blocks.sin"
    if node "$ROOT/tools/verify_palette_blocks.js" "$PB" >/dev/null 2>&1 \
       && "$SINC" "$PB" -o "$WORK/palette_blocks.c" >/dev/null 2>&1 \
       && gcc -c "$WORK/palette_blocks.c" -o "$WORK/palette_blocks.o" >/dev/null 2>&1; then
        echo "✓ palette: 全部积木形状序列化后通过 sinc + gcc 编译"; ((PASS++))
    else
        echo "✗ palette: 积木序列化/编译失败"; ((FAIL++))
    fi
else
    echo "○ 跳过（缺 node）"
fi

echo
echo "=== 发布：编辑器「发布」模态框 → ide_server → 编译产物 ==="
if command -v node >/dev/null 2>&1 && [[ -f /usr/local/lib/libraylib.a ]]; then
    if out="$(NODE_PATH="$(npm root -g)" node "$ROOT/tools/verify_publish.js" linux 2>&1)"; then
        echo "✓ publish: 模态框一键编译出 Linux 产物（$out）"; ((PASS++))
    else
        echo "✗ publish: 发布流水线失败（$out）"; ((FAIL++))
    fi
else
    echo "○ 跳过（缺 node / raylib 桌面库）"
fi

echo
echo "通过 $PASS，失败 $FAIL"
[[ $FAIL -eq 0 ]]
