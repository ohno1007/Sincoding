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

字符串支持字面量（`"..."`，转义 `\n \t \r \" \\`）、变量/参数/返回、`print`，
`+` **拼接**、全部**比较**（`== != < <= > >=`，转 C 走 `strcmp`），
以及内建 **`str(x)`** 把标量（`int/float/bool/string`）转成字符串：

```rust
let score = 42
print("Score: " + str(score))     // Score: 42
print("pi=" + str(3.14))          // pi=3.14
if "apple" < "banana" { ... }     // 字典序比较
```

- `str(x)`：`int→"42"`、`float→"%g"`、`bool→"true"/"false"`、`string→原样`。
- 不做隐式转换：`"x" + n` 非法，须写 `"x" + str(n)`；string 只支持 `+`（不支持 `- * / %`）。
- 内存模型：拼接/转换结果放进运行时的**环形 arena**（无 GC）；结果在 arena 绕回前有效，
  当帧内使用足够。不要把拼接结果长期存起来跨很多帧引用。

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

**数组可像标量一样进出函数（值语义）**：作参数、作返回值、整体赋值都支持，
长度是类型的一部分（`int[3]` 与 `int[5]` 不同类型，不可互传/互赋）。

```rust
fn total(xs: int[3]) -> int {        // 数组作参数
    let s = 0
    for i in 0..3 { s = s + xs[i] }
    return s
}
fn scale(xs: int[3], k: int) -> int[3] {  // 数组作返回值
    let r: int[3]
    for i in 0..3 { r[i] = xs[i] * k }
    return r
}
let a: int[3] = [1, 2, 3]
let b = scale(a, 2)                  // b = [2,4,6]；a 不变（按值拷贝，无别名）
let c: int[3]
c = b                                // 整体赋值
```

> **值语义**：数组按值传递/返回/赋值（整体拷贝），函数内修改参数**不影响调用者**。
> 想「就地更新一个数组」时，用「返回新数组再赋回」的写法（`a = update(a)`）。
> 实现上 C 侧把 `T[N]` 包成 `struct { T data[N]; }`，故可赋值/传参/返回。

- 当前不支持：嵌套数组、对数组整体做运算或 `print`。

### 3.5.1 切片 `T[]`（写通用数组函数）

定长数组的长度属于类型（`int[3]` 与 `int[5]` 不同），所以 `fn sum(xs: int[3])`
只能收三个元素的数组。**切片 `T[]` 解决这个问题**：它是一个「借用视图」
（元素指针 + 长度），任意长度的数组都能传进去。

```rust
fn sum(xs: int[]) -> int {              // 一份代码，任意长度
    let total = 0
    for i in 0..len(xs) { total = total + xs[i] }
    return total
}
fn fill(xs: int[], v: int) {            // 通过切片就地修改调用者的数组
    for i in 0..len(xs) { xs[i] = v }
}

let a: int[3] = [1, 2, 3]
let b: int[5] = [1, 2, 3, 4, 5]
print(sum(a))     // 6   —— 传 int[3] 自动借用为 int[]
print(sum(b))     // 15  —— 同一个函数
fill(a, 7)        // a 变成 [7,7,7]
```

- `len(x)` 取长度：切片取运行时长度，定长数组编译期即知。
- **借用 ≠ 拷贝**：定长数组按值传递（改副本不影响调用者），
  切片是**视图**——通过它修改会改到调用者的数组上。这正是 `sort/reverse/fill`
  能就地工作的原因，用时请留意这点区别。
- 为杜绝悬垂，切片**只能作参数或局部借用**，以下一律拒绝：
  作返回类型、作结构体字段、作全局变量、借用一个临时值（实参必须是变量）。

标准库 [`std/arrayx`](../std/arrayx.sin) 正是基于切片写的通用数组库：
`sum / max_of / min_of / index_of / contains / count_of`（只读）与
`fill / reverse / sort`（就地），外加浮点版 `sum_f / max_of_f`。

## 3.5.2 动态列表 `T[*]`

可增删的列表（Scratch 的"列表"）。v1 规则：**只能声明为全局变量**、从空开始：

```rust
let scores: int[*]          // 全局列表，空起步

fn main() -> int {
    push(scores, 30)         // 追加
    insert(scores, 0, 99)    // 插到下标 0
    remove_at(scores, 0)     // 删除下标 0
    print(scores[0])         // 下标读写（带越界检查）
    print(pop(scores))       // 取出并返回末项（空列表报错）
    print(len(scores))
    clear(scores)
    sort(scores)             // 列表可借用为切片：std/arrayx 直接可用
    return 0
}
```

限制（都会得到明确诊断）：不能在函数内声明、不能作参数/返回值/结构体字段、
不能整体赋值（`a = b` 在 C 里是指针别名，会悬空——要复制请 clear 后逐个 push）。
读写元素/排序请借用为切片 `T[]`；跨函数增删用全局列表。

## 3.5.3 数组越界保护

所有下标访问（定长数组 / 切片 / 列表）都带运行时检查，越界立即报**行号**并终止：

```
运行时错误(第4行): 数组下标 5 越界(长度 3)
```

预览解释器与编译成品同语义（含 `&&`/`||` 短路求值——守卫住的下标不会被求值）。

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

**字段可嵌套**：除标量外，字段还可以是**另一个已声明的结构体**或**标量定长数组**：

```rust
struct Enemy {
    pos: Point,          // 结构体字段（Point 须在前面已声明）
    trail: float[3],     // 标量数组字段
    hp: int
}
let e: Enemy
e.pos = Point { x: 5, y: 9 }   // 字段整体赋值
e.trail = [1.5, 2.5, 3.5]
print(e.pos.x)                 // 链式访问：结构体字段的字段
print(e.trail[1])              // 数组字段下标
```

- 字段可为：标量 / 已声明的结构体 / 标量定长数组 `T[N]`。
- **顺序声明防环**：结构体字段引用的结构体必须在其**前面**声明；不能自引用
  （`struct Node { next: Node }` 报错——无指针，会导致无限大小）。
- 暂不支持：结构体数组字段（`Enemy[3]` 作字段）、数组的数组字段。
- 结构体字面量 `Name { f: v, ... }` 需给全所有字段。
- 在 `if/while/for` 的条件里直接写 `Name { }` 会与代码块歧义，必要时用括号 `(Name { ... })`。

## 3.7 字符串库（内建，按 UTF-8 码点）

无需 import，直接可用；中文一个字算 1（不是 3 个字节）：

```rust
let s = "你好，Sincoding 世界"
print(str_len(s))               // 15
print(str_at(s, 0))             // 你
print(str_sub(s, 3, 9))         // Sincoding（从第 3 个字取 9 个字）
print(str_find(s, "世界"))       // 13（找不到返回 -1）
print(str_contains(s, "coding")) // true
print(str_to_int("42abc"))       // 42
print(str_to_float("3.5"))       // 3.5
```

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

### 数学函数（libm）

C 标准数学库 `libm` 的 `double f(double...)` 函数，签名正好与语言 `float` 对齐，
**直接 `extern fn` 声明即可用**（无需改运行时），程序链接 `-lm`（raylib 成品天然已链接）：

```rust
extern fn sqrt(x: float) -> float
extern fn fabs(x: float) -> float
extern fn fmin(a: float, b: float) -> float
extern fn fmax(a: float, b: float) -> float
// 亦可：sin cos tan floor ceil pow ...
```

常用：`sqrt`（距离）、`sin/cos`（角度）、`fabs`（绝对值）、`fmin/fmax`（clamp）、
`floor/ceil`（取整）、`pow`（幂）。示例见 [`examples/mathx.sin`](../examples/mathx.sin)。
预览解释器（`interp.js`）已内置这些函数的等价实现，编辑期即可试跑。

### 泛型函数

函数可以带**类型参数** `fn f<T>(...)`，编译器按调用点的实际类型**单态化**
出具体实现（如 `total__int` / `total__float`），生成的 C 里全是具体类型，
**零运行时开销**：

```rust
fn total<T>(xs: T[]) -> T {        // 切片 T[] 管长度，泛型 <T> 管元素类型
    let t = xs[0]
    for i in 1..len(xs) { t = t + xs[i] }
    return t
}

let a: int[4] = [1, 2, 3, 4]
let f: float[3] = [1.5, 2.5, 3.0]
print(total(a))     // 10  T=int
print(total(f))     // 7   T=float —— 同一个 total
```

- 类型参数**从实参推断**，调用时不用写 `<int>`。
- 同一类型参数出现多次时，各处实参类型必须一致（否则报「推断冲突」）。
- 类型参数必须出现在**某个参数的类型里**，否则无从推断（会报错）。
- 泛型可以调用泛型（实例化会迭代到不动点）。
- 泛型函数本身不生成代码，只有被调用产生的实例才会；未被调用的泛型不产出任何 C。

## 4.5 模块与标准库（import）

用 `import "模块名"` 复用其它 `.sin` 文件的结构体与函数：

```rust
import "std/mathx"      // 内置标准库（随编译器分发，无需准备任何文件）
import "geom"           // 同目录下的 geom.sin

fn main() -> int {
    print(dist(0.0, 0.0, 3.0, 4.0))   // 5，来自 std/mathx
    return 0
}
```

**标准库本身就是用 Sincoding 写的**（见 [`std/mathx.sin`](../std/mathx.sin)），
构建期嵌入编译器，所以在原生 CLI 与**浏览器编辑器内**都能直接 import。

解析顺序：内置标准库 → 导入方所在目录 `<dir>/<name>.sin` → 环境变量
`SINCODING_PATH`（冒号分隔的目录列表）。

- 支持**嵌套导入**（库自己也能 import），重复导入自动去重（幂等）。
- **循环导入**会被检测并报错。
- 各模块都可能 `extern fn` 借同一个 libm 函数，重复的 extern 原型自动去重，不算冲突。
- 命名空间是**扁平**的：不同模块的同名函数/结构体会被类型检查报「重复定义」。
- `--emit src` 只写回 `import` 行，**不会**把库源码灌进你的文件（往返幂等）；
  积木视图同理只显示你自己的代码。
- **导入即得积木**：编辑器读积木 JSON 的 `libs` 段（被导入函数的签名），
  自动在调色板生成该库的分类——void 函数是语句块，有返回值的是 reporter。

`std/mathx` 现有：`clamp / lerp / sign / dist / dist2`（浮点）与
`abs_i / min_i / max_i / clamp_i`（整数）。

`std/arrayx` 提供通用数组工具（**切片** `T[]` 管长度 + **泛型** `<T>` 管元素类型）：
`sum / max_of / min_of / index_of / contains / count_of / fill / reverse / sort`，
同一份代码同时服务 `int[]` 与 `float[]`。

## 4.6 事件函数（零样板写游戏）

不写 `main` 也能跑：定义**约定名**的事件函数，编译器自动合成主循环
（舞台隐式初始化 480x360；预览与成品驱动顺序完全一致）：

```rust
let hero: int = 0

fn on_start() {                  // 当绿旗被点击：初始化
    hero = sprite_new(0.0, 0.0, 40.0)
}

fn on_frame() {                  // 每一帧：游戏逻辑
    sprite_move(hero, 5.0)
    sprite_bounce(hero)
    sprite_draw(hero)
}

fn on_key_space() { }            // 当按下空格（刚按下的边沿，不是按住）
```

可用事件：`on_start` / `on_frame` / `on_key_space|left|right|up|down` / `on_click`。
事件函数必须无参、无返回值。写了 `main` 则一切照旧（事件函数不会被自动调用）。

### 4.5.1 `.sinlib` 库包

把若干 `.sin` 模块打成**单文件 JSON 库包**分发：

```bash
tools/sinlib.py pack -n "向量库" -o vec.sinlib veclib.sin   # 打包
tools/sinlib.py unpack vec.sinlib -o mylibs/                # 解包（原生编译用）
```

编辑器里「模块 / 库 → 库管理…」安装 `.sinlib` 后 `import "veclib"` 即用
（类型检查/积木生成/预览全链路生效），库随 `.sinproj` 项目保存，发布时自动带上。

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

while cond {
    if done { break }        // 跳出最近一层循环
    if skip { continue }     // 直接进入下一轮
}
```

`if` / `while` 的条件**必须是 `bool`**；`for` 的区间两端必须是 `int`；
`break` / `continue` 只能写在循环里（编译期检查）。

整数 `/` 与 `%` 在除数为 0 时报中文行号错误并终止（预览与成品同语义），
不再是 C 的未定义行为。等待用 `wait(secs: float)`（Scratch 的「等待 x 秒」，
全场冻结，预览与成品一致）。

常量用 `const` 声明（必须给初始值，任何再赋值都会被编译期拒绝）：

```rust
const MAX_HP = 100

fn hurt(hp: int) -> int {
    // MAX_HP = 90      // 错误：不能修改常量
    return hp - 10
}
```

结构体数组/列表的元素字段可以直接赋值：

```rust
foes[i].hp = foes[i].hp - 1
```

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
stmt      ::= let | assign | if | while | for | return | 'break' | 'continue' | expr_stmt
for       ::= 'for' IDENT 'in' expr '..' expr block
let       ::= ('let' | 'const') IDENT (':' type)? ('=' expr)? ';'?
assign    ::= IDENT ('[' expr ']')? ('.' IDENT)? '=' expr ';'?
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
