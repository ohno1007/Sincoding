# 从积木到原生 exe：Sincoding 全链路走一遍

Sincoding 是一门**静态类型的小语言 + 图形化编辑器**：用积木或文本写程序，
经自研编译器转成 C，再编成各平台**原生成品**（Windows / Web / Linux / Android）。

它和 Scratch 最大的不同：Scratch 在浏览器里**解释运行**，做不出独立成品；
Sincoding 是**真编译**——同一份程序能出可玩的网页 wasm、也能出原生 `.exe`。

---

## 1. 一张图看懂链路

```
积木  ⇄  文本            ← 编辑期（AST 唯一真相，双向同步）
  └──────┘
      │ AST
      ▼
  自研编译器 (C++)：Lexer → Parser → 类型检查 → 生成 C
      │
      ├─ 预览：interp.js 浏览器内解释（编辑即跑）
      │
      ▼  生成的 C  +  runtime（raylib 封装）
  ┌──────────┬──────────┬──────────────┬──────────┐
  │ Linux    │ Windows  │ Web(wasm)    │ Android  │
  │ 原生二进制 │  .exe    │ .html+.wasm  │  .apk    │
  └──────────┴──────────┴──────────────┴──────────┘
```

**核心原则**：AST 是唯一真相，积木是它的可视化渲染，文本是它的序列化，二者绝不直接互转。

---

## 2. 以 guardian 小游戏为例

「家园守护者」：方向键移动挡板，接金币(+分)、躲炸弹(-命)。
源码 [`examples/guardian.sin`](../examples/guardian.sin)。

### 2.1 用「正经结构」建模

把一个下落物的完整状态聚成结构体，用**结构体数组**存所有下落物
（而不是几个平行的全局数组）：

```rust
struct Faller {
    x: float,
    y: float,
    kind: int,        // 0=金币 1=炸弹
    handle: int
}

let fallers: Faller[6]           // 结构体数组 = "敌人列表"

// 结构体作参数 + 返回值（值语义：按值拷贝，无别名坑）
fn respawn(f: Faller, seed: int) -> Faller {
    f.x = spawn_x(seed)
    f.y = 280.0
    return f
}
fn caught(f: Faller, px: float) -> bool {
    return abs_f(f.x - px) < 60.0
}
```

主循环用「取出 → 改 → 放回」处理每个下落物：

```rust
for i in 0..N {
    let f = fallers[i]           // 取出（值拷贝）
    f.y = f.y - fall
    if f.y < -240.0 {
        if caught(f, px) {
            if f.kind == 0 { game.score = game.score + 1 }
            if f.kind == 1 { game.lives = game.lives - 1 }
        }
        f = respawn(f, game.score + i * 3 + game.level)
    }
    sprite_move_to(f.handle, f.x, f.y)
    fallers[i] = f               // 放回
}
```

### 2.2 编辑期：积木 ⇄ 文本 + 真 IDE

编辑器里左边拖积木、右边同步显示等价文本（`sinc --emit src` 逐字节一致）：

![积木/文本双向同步](images/stage3_ide.png)

IDE 具备 Scratch 没有的「真编程」能力：

- **悬停/光标显示类型**：光标停在 `fallers` 上，状态栏显示 `变量 fallers : Faller[6]`
- **查找引用 / F2 重命名**：改 `Faller` 名字，所有类型标注/字面量一起改；
  改局部变量只影响当前函数（作用域正确）
- **补全 + 类型诊断**：类型不匹配即时报错并可跳转

### 2.3 编译发布

```bash
# 先构建编译器
cmake -S compiler -B compiler/build && cmake --build compiler/build

# 原生 Linux（需 raylib）
tools/build_native.sh examples/guardian.sin out/guardian

# Web(wasm)（需 emscripten + raylib-web）；末参数是造型资源目录
tools/build_web.sh examples/guardian.sin out/guardian-web examples/assets/guardian

# Windows .exe（需 MinGW-w64 + raylib-win）
tools/build_windows.sh examples/guardian.sin out/guardian.exe

# Android arm64 .so（需 NDK + raylib-android）→ 放进 APK 的 jniLibs/<abi>/
tools/build_android.sh examples/guardian.sin out/libsincoding.so
# 一步打成可安装签名 APK（另需 Android SDK build-tools）
tools/build_apk.sh examples/guardian.sin out/guardian.apk Guardian
```

**四端交叉编译均已端到端验证**（`tests/run_tests.sh`，缺工具链则跳过）：

| 目标 | 产物 | 验证方式 |
|---|---|---|
| Linux | 原生二进制 | 无头 raylib 渲染 + 计分逻辑 |
| Web | `.html + .wasm + .data` | Chromium 渲染截图（含造型预载） |
| Windows | `.exe` | PE32+ MS Windows 可执行 |
| Android | `.so`（→ APK） | ARM aarch64 + 导出 `ANativeActivity_onCreate` |

编辑器里也可以「一键发布」弹窗勾选平台；**单文件 HTML** 目标完全在浏览器内生成
（内联解释器 + 程序 + 造型），双击即玩、免任何工具链。

### 2.4 成品：网页里真的在跑

guardian 编成 WebAssembly 后在浏览器里的真实画面——造型 PNG 随包加载，
金币/炸弹下落、挡板、计分板全部正常：

![guardian 网页版](images/guardian_web.png)

---

## 3. 语言能力速览

| 类别 | 支持 |
|---|---|
| 类型 | `int / float / bool / string / void`、结构体、定长数组 `T[N]` |
| 聚合 | 数组/结构体作参数、返回值、整体赋值（值语义）；结构体数组、嵌套字段 |
| 字符串 | 拼接 `+`、比较、`str(x)` 转换（运行时环形 arena） |
| 数学 | libm 直接 `extern fn` 声明可用（`sqrt/sin/cos/fmin/fmax/...`） |
| 运行时 | 舞台/精灵/造型/键鼠/画笔/声音/广播/碰撞 `sprite_touching` |
| 互操作 | `extern fn` FFI + JSON 桥接外部 ELF（静态 / dlopen 动态） |

完整语言参考见 [`docs/LANGUAGE.md`](LANGUAGE.md)，方向与架构见 [`docs/ROADMAP.md`](ROADMAP.md)。

---

## 4. 端到端测试

`tests/run_tests.sh` 覆盖整条流水线：源码 → C → 编译 → 运行 → 比对输出，
含序列化幂等、积木往返、类型错误反例、原生/Web 无头渲染、IDE 交互（Chromium）、
mini-LSP（hover/rename）等。缺工具链的阶段自动跳过。

```bash
bash tests/run_tests.sh
```
