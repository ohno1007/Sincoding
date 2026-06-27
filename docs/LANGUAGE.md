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
| `void` | 无返回值（仅用于函数返回类型） | `void` |

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
```

`if` / `while` 的条件**必须是 `bool`**。

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
| `print(x)` | 打印 `int` / `float` / `bool`，自动换行 |

> 后续阶段会把 `runtime/` 的 `rt_*` Scratch API（移动精灵、按键、声音等）
> 作为内建/外部函数接入，并通过 JSON 桥接外部 ELF 扩展更多函数。

## 8. 注释

```rust
// 这是行注释，直到行尾
```

## 9. 文法（EBNF 概要）

```
program   ::= fn_decl*
fn_decl   ::= 'fn' IDENT '(' params? ')' ('->' type)? block
params    ::= param (',' param)*
param     ::= IDENT ':' type
type      ::= 'int' | 'float' | 'bool' | 'void'
block     ::= '{' stmt* '}'
stmt      ::= let | assign | if | while | return | expr_stmt
let       ::= 'let' IDENT (':' type)? '=' expr ';'?
assign    ::= IDENT '=' expr ';'?
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
primary   ::= INT | FLOAT | 'true' | 'false' | IDENT
            | IDENT '(' args? ')' | '(' expr ')'
args      ::= expr (',' expr)*
```

分号 `;` 在语句末尾可选——换行即可分隔语句。
