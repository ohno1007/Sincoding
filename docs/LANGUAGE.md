# Sincoding 语言参考（MVP）

Sincoding 是一门**静态类型**的小语言：显式类型标注 + 局部类型推断，
语法风格接近 Lua/Python，但用**大括号**分块（转 C 更省事）。
本文档描述当前已实现的子集（路线图阶段 1）。

## 1. 程序结构

一个程序由若干**函数声明**组成，必须包含入口函数 `main`：

```rust
fn main() -> int {
    return 0
}
```

## 2. 类型

| 类型 | 含义 | 对应 C 类型 |
|---|---|---|
| `int` | 64 位有符号整数 | `long long` |
| `float` | 双精度浮点 | `double` |
| `bool` | 布尔 | `bool` (`stdbool.h`) |
| `string` | 字符串（字面量，UTF-8） | `const char*` |
| `void` | 无返回值（仅用于函数返回类型） | `void` |

字符串支持字面量（`"..."`，转义 `\n \t \r \" \\`）、变量/参数/返回、`print`、
以及 `==` / `!=` 比较（转 C 走 `strcmp`）。暂不支持拼接与大小比较（避免引入内存管理）。

不同类型之间**不做隐式转换**（例如 `int` 与 `float` 不能直接相加），
以避免动态类型转 C 的复杂度爆炸。

## 3. 变量与类型推断

```rust
let x = 5            // 推断为 int
let y: float = 3.0   // 显式标注
let ok = true        // 推断为 bool
x = x + 1            // 赋值（类型必须一致）
```

- `let name = expr`：从初始化表达式推断类型。
- `let name: T = expr`：显式标注，且要求 `expr` 类型为 `T`。
- 变量先声明后使用；同一作用域内不可重复声明。

## 3.5 数组（定长列表）

定长、栈分配，不引入堆分配/GC——适合分数表、敌人列表等：

```rust
let xs: int[5] = [2, 4, 6, 8, 10]   // 字面量初始化
let g: int[4]                        // 省略初始化 → 全部置 0
let ys = [1, 2, 3]                   // 推断为 int[3]

g[0] = 7                             // 写元素
let v = xs[2] + g[0]                 // 读元素（下标须为 int）
```

- 类型写作 `T[N]`（`T` 为 `int/float/bool/string`，`N` 为长度）。
- 越界访问不做运行时检查（同 C），由编写者负责。
- 当前不支持：嵌套数组、把数组作为参数/返回值、对数组整体做运算或 `print`。

## 3.6 结构体（简单 / 标量字段）

把若干标量字段聚合为一个类型，可作变量、参数与返回值（按值传递）：

```rust
struct Point {
    x: int,
    y: int
}

fn make(a: int, b: int) -> Point {
    return Point { x: a, y: b }     // 结构体字面量
}

fn main() -> int {
    let p = make(3, 4)
    print(p.x)          // 字段读取
    p.y = 10            // 字段写入
    let q: Point        // 零初始化（全字段为 0）
    return 0
}
```

- 字段必须是标量（`int/float/bool/string`），暂不支持嵌套结构体/数组字段。
- 结构体字面量 `Name { f: v, ... }` 需给全所有字段。
- 在 `if/while/for` 的条件里直接写 `Name { }` 会与代码块歧义，必要时用括号 `(Name { ... })`。

## 4. 函数

```rust
fn add(a: int, b: int) -> int {
    return a + b
}

fn greet() {          // 省略 -> 表示返回 void
    print(1)
}
```

- 参数必须显式标注类型。
- 省略 `-> T` 等价于返回 `void`。
- 支持递归与互递归（函数签名在检查函数体前先全部收集）。

### 外部函数（FFI）

用 `extern fn` 声明没有函数体的外部函数，转译时生成 `extern` 原型，
链接期绑定到运行时（`runtime/prelude`）或外部库的实现：

```rust
extern fn stage_init(w: int, h: int)
extern fn sprite_new(x: float, y: float, size: float) -> int
extern fn key_down(key: int) -> bool
```

ABI 约定：语言 `int` → C `long long`，`float` → C `double`，`bool` → C `bool`。
这是语言访问 raylib 运行时（开窗口、画角色、读输入）以及未来 JSON 桥接外部 ELF 的统一入口。
完整运行时函数清单见 [`runtime/prelude.h`](../runtime/prelude.h)，示例见 [`examples/game.sin`](../examples/game.sin)。

## 5. 控制流

```rust
if cond {
    // ...
} else if other {
    // ...
} else {
    // ...
}

while cond {
    // ...
}

for i in 0..n {     // 区间 for，i 从 0 到 n-1（右开）
    // ...
}
```

`if` / `while` 的条件**必须是 `bool`**；`for` 的区间两端必须是 `int`。

### 全局变量

在顶层（函数外）用 `let` 声明的变量是全局的，所有函数可读写（初始化需为常量表达式）：

```rust
let high_score = 0

fn add_score(n: int) -> int {
    high_score = high_score + n
    return high_score
}
```

## 6. 运算符（按优先级从低到高）

| 优先级 | 运算符 | 说明 |
|---|---|---|
| 1 | `\|\|` | 逻辑或（要求 bool） |
| 2 | `&&` | 逻辑与（要求 bool） |
| 3 | `==` `!=` | 相等比较（两侧同类型，结果 bool） |
| 4 | `<` `<=` `>` `>=` | 大小比较（结果 bool） |
| 5 | `+` `-` | 加减 |
| 6 | `*` `/` `%` | 乘除取模（`%` 仅限 int） |
| 7 | 一元 `-` `!` | 取负 / 逻辑非 |

## 7. 内建函数

| 函数 | 说明 |
|---|---|
| `print(x)` | 打印 `int` / `float` / `bool` / `string`，自动换行 |

运行时（图形）函数通过 `extern fn` 接入（实现在 `runtime/prelude`），例如：
`sprite_load(path: string)` 加载 PNG 造型为纹理、`say(s, text: string)` 气泡、
`draw_text(text: string, x, y, size)` 画文字，见 [`examples/say.sin`](../examples/say.sin)。

> 图形/输入/声音等 Scratch API 通过 `extern fn` 接入（见上文 FFI 一节），
> 实现在 `runtime/prelude`。后续会再通过 JSON 桥接外部 ELF 扩展更多函数。

## 8. 注释

```rust
// 这是行注释，直到行尾
```

## 9. 文法（EBNF 概要）

```
program     ::= (struct_decl | fn_decl | let)*
struct_decl ::= 'struct' IDENT '{' (IDENT ':' type ','?)* '}'
fn_decl     ::= 'extern'? 'fn' IDENT '(' params? ')' ('->' type)? (block | ';'?)
params    ::= param (',' param)*
param     ::= IDENT ':' type
type      ::= ('int' | 'float' | 'bool' | 'string' | 'void') ('[' INT ']')?
block     ::= '{' stmt* '}'
stmt      ::= let | assign | if | while | for | return | expr_stmt
for       ::= 'for' IDENT 'in' expr '..' expr block
let       ::= 'let' IDENT (':' type)? ('=' expr)? ';'?
assign    ::= IDENT ('[' expr ']')? '=' expr ';'?
if        ::= 'if' expr block ('else' (if | block))?
while     ::= 'while' expr block
return    ::= 'return' expr? ';'?
expr_stmt ::= expr ';'?

expr      ::= or
or        ::= and ('||' and)*
and       ::= equality ('&&' equality)*
equality  ::= comparison (('==' | '!=') comparison)*
comparison::= term (('<' | '<=' | '>' | '>=') term)*
term      ::= factor (('+' | '-') factor)*
factor    ::= unary (('*' | '/' | '%') unary)*
unary     ::= ('-' | '!') unary | primary
postfix   ::= primary ('[' expr ']' | '.' IDENT)*    // 下标 / 字段访问
primary   ::= INT | FLOAT | STRING | 'true' | 'false'
            | IDENT '(' args? ')'                    // 函数调用
            | IDENT '{' (IDENT ':' expr ','?)* '}'   // 结构体字面量
            | IDENT                                  // 变量
            | '[' (expr (',' expr)*)? ']'            // 数组字面量
            | '(' expr ')'
args      ::= expr (',' expr)*
```

分号 `;` 在语句末尾可选——换行即可分隔语句。
