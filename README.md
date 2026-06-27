# Sincoding

类似 Scratch 的图形化编程工具：用户通过**积木**或**文本**编写程序，
程序使用自研语言（语法类似 Lua/Python），**转译为 C** 后编译为各平台原生应用。

> 完全自研 · imgui + Qt 编辑器 · raylib 运行时 · 多平台（Windows / Android / Web）

详细技术设计见 [`docs/DESIGN.md`](docs/DESIGN.md)，语言参考见 [`docs/LANGUAGE.md`](docs/LANGUAGE.md)。

---

## 当前进度

按照路线图，**地基（语言 → C 转译器）已经跑通**：

| 阶段 | 内容 | 状态 |
|---|---|---|
| 1 | 语言 → C 转译器（Lexer/Parser/类型检查/代码生成） | ✅ 已完成，斐波那契等用例可编译运行 |
| 2 | runtime.c 基于 raylib（舞台/精灵/输入/声音） | 🚧 API 已设计并落地（`runtime/`），待接入构建 |
| 3 | 积木编辑器接 AST（积木 ⇄ 文本双向同步） | ⏳ 规划中 |
| 4 | CMake 多平台（Windows → Web → Android） | ⏳ 规划中 |
| 5 | JSON 桥接外部 ELF（静态 + 动态） | ⏳ 规划中 |

核心设计原则：**AST 是唯一真相源**。积木是 AST 的可视化渲染，文本是 AST 的序列化。

---

## 快速开始

### 1. 构建编译器 `sinc`

```bash
cmake -S compiler -B compiler/build
cmake --build compiler/build -j
```

### 2. 把 Sincoding 源码转译为 C 并运行

```bash
# 转译为 C
./compiler/build/sinc examples/fib.sin -o fib.c

# 用任意 C 编译器编译运行
gcc fib.c -o fib && ./fib
# 输出:
# 55
# 55
```

也可以直接打印生成的 C（不带 `-o` 时输出到 stdout），或用 `--tokens` 查看词法结果。

### 3. 跑测试

```bash
bash tests/run_tests.sh
```

端到端验证：源码 → 转译 → `gcc` 编译 → 运行 → 比对输出，并检查类型/语义错误能被正确拒绝。

---

## 语言一瞥

显式类型 + 局部类型推断，大括号风格：

```rust
fn fib(n: int) -> int {
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

fn main() -> int {
    let n = 10          // 推断为 int
    print(fib(n))       // 55
    return 0
}
```

支持：`int` / `float` / `bool`、函数（含递归/互递归）、`if/else`、`while`、
算术与逻辑运算、内建 `print`。完整文法见 [`docs/LANGUAGE.md`](docs/LANGUAGE.md)。

---

## 目录结构

```
compiler/        语言 → C 转译器（C++17，手写递归下降）
  include/       词法/语法/AST/类型检查/代码生成 头文件
  src/           对应实现 + sinc 命令行入口
runtime/         Scratch 风格运行时 runtime.c（封装 raylib）
examples/        示例 .sin 程序
tests/           端到端测试与用例
docs/            技术设计与语言参考
```
