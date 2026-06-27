# 图形化编程编辑器 — 技术设计文档

一个类似 Scratch 的图形化编程工具：用户通过积木或文本编写程序，
程序使用自定义语言（语法类似 Lua/Python），转译为 C 后编译为各平台原生成品应用。

---

## 1. 项目定位

| 维度 | 决策 |
|---|---|
| 产物类型 | 类 Scratch 的 2D 图形小程序/小游戏（精灵、舞台、键鼠输入、声音） |
| 自定义语言 | 语法类似 Lua/Python，**显式类型 + 局部类型推断** |
| 编译目标 | 转译为 C，再编译为原生二进制 |
| 发布平台 | Windows、Android、Web(wasm)；**iOS 暂放弃**（无 Mac 环境） |
| 积木编辑器 | 原生桌面，imgui + Qt |
| 外部扩展 | JSON 接口定义桥接外部 ELF（dlopen 动态 + 静态链接，两者都支持） |
| 图形运行时 | **raylib**（统一跨平台外壳） |

---

## 2. 整体架构

```
┌─────────────────────────────────────────┐
│  积木编辑器 (imgui + Qt)                   │  ← 开发期工具
│  积木拖拽 ⇄ 文本编辑，双向同步              │
├─────────────────────────────────────────┤
│  AST (唯一真相源 Single Source of Truth)   │
├─────────────────────────────────────────┤
│  你的语言 → C 转译器                        │
│  Lexer → Parser → AST → 类型检查 → C 代码   │
├─────────────────────────────────────────┤
│  runtime.c  (Scratch 风格 API，封装 raylib) │  ← 核心库
├─────────────────────────────────────────┤
│  raylib  (跨平台图形 / 输入 / 音频)          │
├─────────────────────────────────────────┤
│  CMake  统一构建                            │
├──────────────┬──────────────┬────────────┤
│  Windows.exe │  Android.apk │  Web(wasm)  │  ← 成品
└──────────────┴──────────────┴────────────┘
                    ▲
        JSON 接口定义 → 桥接外部 ELF
```

核心设计原则：**AST 是唯一真相源**。积木是 AST 的可视化渲染，文本是 AST 的序列化，
两者都通过 AST 互转，绝不直接做积木↔文本转换。

---

## 3. 数据流

```
积木拖拽 ──┐
          ├──► 修改 AST ──► 重新布局积木 / 序列化为文本
文本编辑 ──┘                    │
                              ├──► 类型检查
                              ▼
                          生成 C 代码
                              │
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
         链接 runtime.c   链接 raylib    链接外部 ELF
              │               │          (JSON 定义)
              └───────────────┼───────────────┘
                              ▼
                         CMake 构建
                              │
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
         Windows.exe     Android.apk      Web.wasm
```

---

## 4. 子系统设计

### 4.1 语言 / 编译器（项目地基，最先做）

**编译流程**

```
源码 → Lexer → Parser → AST → 类型检查 → C 代码生成 → gcc/clang/emcc → 二进制
```

**实现要点**

- 手写**递归下降 Parser**，不用 yacc/bison。原因：AST 要双向同步积木，必须完全可控；
  且编辑器需要良好的错误恢复（语法错时仍能产出部分 AST）。
- AST 节点用带 tag 的结构体或 `std::variant`。
- 用 C/C++ 实现，与 imgui+qt、raylib 同生态。

**类型系统：显式类型 + 局部推断**

```rust
let x = 5          // 推断为 int
let y: float = 3   // 显式标注
fn add(a: int, b: int) -> int {
    return a + b
}
```

> 不要做纯动态类型。纯动态转 C 需要 tagged union + 运行时类型判断，
> 复杂度和性能都不可接受。初期锁定静态类型。

**内存管理**（转 C 没有 GC，按复杂度递增）

1. Arena / 区域分配 —— 最简单，适合短生命周期程序，**初期首选**
2. 引用计数 —— 中等，注意循环引用
3. 完整 GC —— 初期不做

**语法风格**：转 C 时用**大括号**比缩进更省事（缩进解析更复杂）。

**MVP 里程碑**：支持 `int/float/bool`、函数、`if/while`、算术 →
能转出可编译的 C，跑通 hello world 与斐波那契。

### 4.2 积木 UI 与 AST 同步

**核心原则**：AST 是唯一真相，积木只是渲染器。

- 积木拖拽 → 修改 AST → 重新布局
- 文本编辑 → reparse → 新 AST → 重渲染积木
- **imgui** 做积木渲染（即时模式适合频繁重布局）；**Qt** 做外壳、菜单、文件树、工程管理
- 文本有语法错误时 AST 不完整，积木侧需显示"部分有效"状态

### 4.3 运行时库 runtime.c（核心）

用户的语言**不直接**调用 raylib，而是调用一层 Scratch 风格的运行时 API。
用户写 `move(sprite, 10)`，转出的 C 调用 `rt_move()`，内部再调 raylib。
好处：用户接触不到 raylib；以后换底层渲染库只改 runtime，不影响用户程序。

**Scratch 概念 → raylib 映射**

| Scratch 概念 | runtime 实现（基于 raylib） |
|---|---|
| 舞台 / 绿旗运行 | `InitWindow` + 主循环 `while(!WindowShouldClose())` |
| 角色（精灵） | `Texture2D` + 位置/旋转/缩放结构体 |
| 移动 / 旋转 | 改坐标，`DrawTextureEx` |
| 当按下按键 | `IsKeyDown()` |
| 广播 / 事件 | 自建事件队列 |
| 播放声音 | `PlaySound()` |
| 说 / 想（气泡） | `DrawText()` |
| 循环执行 | 主循环里每帧调用 |

**为什么选 raylib 而非 SDL2**

- 纯 C，与转出的 C 无缝对接，无需 C++ 胶水
- 抽象层级与 Scratch 几乎一一对应
- 官方支持 Windows/Linux/macOS/Android/Web(wasm)，正好匹配目标平台
- 依赖少、易静态链接成单文件成品

### 4.4 JSON 桥接外部 ELF

**接口定义示例**

```json
{
  "module": "motor",
  "link": "static",
  "header": "motor.h",
  "library": "libmotor.so",
  "functions": [
    {
      "name": "setSpeed",
      "symbol": "motor_set_speed",
      "params": [{ "name": "v", "type": "int" }],
      "ret": "void"
    }
  ]
}
```

- **静态链接**：代码生成时产出 `extern` 声明 + 链接时 `-lmotor`
- **动态加载**：生成 `dlopen` + `dlsym` 包装代码，通过函数指针调用
- **关键**：维护一张类型映射表，把 JSON 类型 ↔ 语言类型 ↔ C 类型对应起来

### 4.5 多平台构建（CMake 统一）

| 平台 | 工具链 | 产物 | 难度 | 说明 |
|---|---|---|---|---|
| Windows | MinGW-w64 / MSVC | `.exe` | 低 | 可在 Linux 交叉编译；raylib 静态链接出单文件 |
| Web(wasm) | Emscripten (emcc) | `.html + .wasm` | 低 | 最像 Scratch 的发布方式，建议优先级提前 |
| Android | NDK + Gradle | `.apk` | 中 | C+runtime 编进 `.so`，套官方 APK 壳 |
| iOS | Xcode（需 macOS） | `.ipa` | — | **暂放弃**，无 Mac 环境 |

**预置模板工程**：每个平台一套骨架，构建时注入生成的 C / so。

```
templates/
  windows/   → CMakeLists + main 入口
  web/       → emscripten shell.html + CMakeLists
  android/   → gradle 工程 + JNI 桥 + CMakeLists
```

---

## 5. 开发路线图

| 阶段 | 任务 | 验证目标 |
|---|---|---|
| 0 | **最小垂直切片** | 一段语言代码 → 转 C → 调 raylib runtime → 编出能用方向键移动精灵的 .exe |
| 1 | 语言 → C 转译器 | 支持基本类型/函数/控制流/算术，跑通斐波那契 |
| 2 | runtime.c 基于 raylib | 窗口 + 画精灵 + 键盘移动 + 声音 |
| 3 | 积木编辑器接 AST | 积木 ⇄ 文本双向同步 |
| 4 | CMake 多平台 | 先 Windows，再 Web(wasm)，最后 Android |
| 5 | JSON 桥接 ELF | 静态 + 动态两种外部库接入 |

> **阶段 0 是最该先证明的事**：它一次性验证整条技术栈
> （语言→C→runtime→raylib→编译→成品）是否成立。

---

## 6. 关键技术风险

| 风险 | 应对 |
|---|---|
| 动态类型转 C 复杂度爆炸 | 锁定静态类型（显式 + 局部推断），初期不碰动态 |
| 内存管理（无 GC） | 初期用 Arena 分配，后续再引入引用计数 |
| 积木与文本同步出错 | 强制以 AST 为唯一真相，禁止积木↔文本直转 |
| Android 打包繁琐 | 用 raylib 官方 NDK 模板 + 预置 APK 工程 |
| iOS 无法构建 | 明确放弃；未来如需，走云 Mac / CI |
| 外部 ELF 类型不匹配 | 维护严格的 JSON↔语言↔C 类型映射表 |
