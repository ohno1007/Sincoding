# Sincoding 路线图与架构 —— 「真可视化编程」方向

> 方向已定：做**面向想学/想用真编程的人**的可视化编译器。
> 保留静态类型，强化语言与 IDE 能力，不迁就低龄；差异化是「拖积木写真代码，一键编出原生程序」。
> 本文档给出目标架构、关键技术决策与逐里程碑的实施计划。
> 基线代码：`claude/graphical-editor-design-1x0uog` 分支（commit `c7afb97`）。

---

## 0. 现状盘点（实测）

| 模块 | 规模 | 现状 |
|---|---|---|
| `compiler/` (C++) | ≈2.7k 行 | lexer 232 / parser 505 / type_checker 478 / codegen 328 / serializer 534 / wasm_api 69 |
| `runtime/` (C, raylib) | ≈600 行 | prelude ≈60 个 API（舞台/精灵/输入/画笔/声音/广播） |
| `editor/` (纯 JS) | ≈3k 行 | app.js 1560 / interp.js 353 / blocks_data 266 / paint 220 / blockmodel 132 |
| `desktop/` (Go) | 79 行 | 内嵌 editor 的单文件可执行 |
| 测试 | run_tests.sh 401 行 + 9 个错误用例 + verify_*.js | 端到端：.sin → C → 二进制 → 比对输出 |

**已验证成立的东西（不要动摇）：**

1. AST 唯一真相源，积木 ⇄ 文本双向同步（wasm 引擎反向解析）。
2. 语言 → C → raylib → Windows/Android/Web/单文件 HTML 全链路发布。
3. 预览 = 成品（interp.js 与 runtime 同源语义、共享造型）。

**当前语言的硬天花板（`docs/LANGUAGE.md` 自述 + 代码核实）：**

- 数组不能作参数/返回值，不能嵌套，不能整体赋值。
- 结构体只能有标量字段（不能嵌套结构体、不能含数组）。
- 字符串不能拼接、不能大小比较。
- 根因：类型表示是**扁平的**——`ast.h` 里 `enum class Type` + `arrayLen` + `structName`
  三个字段拼在 `Expr` 上，表达不了 `Point[8]`、`Enemy{pos: Point}` 这类**递归类型**。

**当前架构的最大隐患：**

- `blockmodel.js` 的 `modelToSource` **镜像** C++ `serializeSource`（双实现，靠测试对齐）。
  语言每加一个特性都要写两遍序列化，长期必然漂移。

---

## 1. 目标架构

```
┌────────────────────────────────────────────────────────────┐
│                    Web IDE（正式产品，非原型）                 │
│  积木画布 · 文本编辑器 · 调色板 · 舞台预览 · 调试器 · 发布      │
│  （纯视图/控制器：不再持有任何语言知识）                        │
└──────────────┬─────────────────────────────┬───────────────┘
               │ 引擎 API（wasm 导出）          │ 预览
               ▼                             ▼
┌──────────────────────────────┐   ┌─────────────────────────┐
│   唯一语言引擎（C++，单实现）    │   │  interp.js（积木解释器）  │
│  lexer → parser → AST         │   │  运行高亮/单步/变量监视    │
│  → 类型检查 → 诊断             │   │  语义与 runtime 对齐      │
│  → C 代码生成                  │   └─────────────────────────┘
│  → 源码序列化（唯一序列化器）    │
│  → 积木模型导出                 │
│  → 查询：符号/悬停/跳转/改名     │   ← M3 新增（mini-LSP）
│  → 模块解析 / 签名→积木生成      │   ← M4 新增（库生态）
│  同一份代码编两份：              │
│    sinc（native CLI）          │
│    sinc.wasm（浏览器内）        │
└──────┬───────────────────────┘
       │        ▲
       │   ┌────┴─────────────────────────┐
       │   │ 库生态（M4）                    │
       │   │  std/*（用 Sincoding 自写）     │
       │   │  用户 .sinlib（src + blocks.json│
       │   │  + 可选 native/ JSON ELF 桥）   │
       ▼   └──────────────────────────────┘
┌──────────────────────────────┐
│  runtime/prelude（C, raylib）  │  Scratch 风格 API ≈60 个
│  + debug 构建：imgui overlay   │  ← M5 新增（F12 调试面板）
└──────────────┬───────────────┘
               ▼
┌─────────┬─────────┬──────────┬──────────────┐
│ Linux   │ Windows │ Android  │ Web / 单文件HTML │  ← CMake + build_*.sh
└─────────┴─────────┴──────────┴──────────────┘
```

### 三条架构不变量（所有改动必须遵守）

1. **AST 唯一真相**：积木是渲染，文本是序列化，二者只经 AST 互转。
2. **引擎单例**：语言知识（语法/类型/序列化/查询）只存在于 C++ 引擎；
   JS 侧的 `blockmodel.js` 镜像是技术债，按 M3 计划逐步退役。
3. **预览 = 成品**：interp.js 每加一个运行时函数，runtime.c 必须同步，反之亦然；
   由 `tools/verify_palette_blocks.js` 类测试强制。

### 明确冻结/放弃的决策

| 决策 | 理由 |
|---|---|
| **imgui 不做积木编辑器，改做原生调试面板** | 即时模式+矩形控件做异形积木是逆水行舟；但做**每帧重画的调试/监视面板**正是 imgui 的看家本领。定位：原生成品 debug 构建里的 overlay 调试器（见 D6），积木编辑器保持 Web IDE。桌面版继续走 Go 内嵌（已可出单文件 exe）。 |
| **不做动态类型 / 不做 GC** | 与「真编程」方向一致；聚合类型走值语义（见 §2），内存模型保持可教学、可预测。 |
| **不追 Scratch 生态**（素材库/社区/云变量） | 差异化在编译器与 IDE，不在低龄生态。素材只补到「够做 demo」。生态的重心放在**库生态**（见 D5）：让用户自己写库、互相导入。 |

---

## 2. 关键技术决策（先拍板，后编码）

### D1 类型表示改为递归 TypeRef（一切的地基）

```cpp
// 替换 ast.h 的扁平 Type + arrayLen + structName
struct TypeRef {
    enum Kind { Int, Float, Bool, String, Void, Struct, Array, Unknown } kind;
    std::string structName;            // kind == Struct
    std::unique_ptr<TypeRef> elem;     // kind == Array 的元素类型（可再嵌套）
    int len = 0;                       // kind == Array 的定长
};
```

能表达 `int[5]`、`Point[8]`、`struct Enemy { pos: Point, trail: float[16] }`、
`Enemy[32]`——真实游戏建模所需的全部组合。

### D2 聚合类型采用**值语义**，数组在 C 侧用结构体包裹

转 C 时数组不裸奔（裸数组会退化成指针、不能返回/赋值），统一包成：

```c
typedef struct { long long data[5]; } Arr_int_5;
```

- 数组从此和结构体一样：**可赋值、可传参、可返回**，语义统一为按值拷贝。
- 学习者心智模型简单：「一切皆值，赋值即拷贝」，没有指针/别名坑。
- 性能：教学规模（N ≤ 几百）+ `-O2` 下拷贝成本可忽略；确有热点再引入引用传参优化。
- `a[i]` 生成 `a.data[i]`；类型名规范化（`Arr_<elem>_<len>`，嵌套递归展开）。

### D3 字符串引入**运行时 arena**，解锁拼接/比较

- runtime 增加字符串 arena（帧循环程序每帧重置；CLI 程序退出时释放）。
- 语言侧开放：`s1 + s2` 拼接、`< <= > >=` 比较（`strcmp`）、`len(s)`、`str(n)` 数字转字符串。
- 不引入 GC，不引入所有权——arena 生命周期规则一句话讲清：「字符串活到帧末」。

### D4 引擎升级为 mini-LSP（wasm_api 从 69 行扩成查询层）

现 `wasm_api.cpp` 只有 parse/emit。扩展为：

| API | 用途 |
|---|---|
| `queryDocumentSymbols()` | 大纲视图（函数/结构体/全局） |
| `queryHover(line, col)` | 悬停显示推断类型 |
| `queryDefinition(line, col)` | 跳转到定义 |
| `queryReferences(line, col)` | 查找引用 |
| `applyRename(line, col, newName)` | 按作用域安全改名，返回新 AST |
| `applyEdit(op)` | 结构化 AST 编辑（积木拖拽走这里，替代 blockmodel.js） |

`applyEdit` 是退役 `blockmodel.js` 镜像的关键：积木编辑发结构化操作给引擎，
引擎改 AST 后返回新文本 + 新积木模型，**序列化只剩 C++ 一份实现**。

### D5 模块与库生态：标准库用 Sincoding 自己写，导入即得积木

**原则：语言的库尽量用语言自己写。** 只有触硬件/OS 的部分才下沉到 C runtime
（extern fn），纯算法（排序、查找、队列/栈、寻路、数学工具）一律 `.sin` 实现——
这既是吃自己狗粮的最强测试，也让用户「写库」和「写程序」是同一件事。

**1）多文件与导入**

```rust
import "mathx"        // 导入同项目 / 已安装库里的模块
import "algo/sort"    // 库内子模块

fn main() -> int {
    let xs = [3, 1, 2]
    sort_int(xs, 3)    // 来自 algo/sort
    return 0
}
```

- 编译器支持多文件编译单元：模块解析 → 符号合并 → 重名冲突显式诊断。
- MVP 用扁平命名空间（重名报错），后续再考虑 `mod.func` 限定语法。

**2）库包格式 `.sinlib`**（zip）

```
mylib.sinlib
├── lib.json          # 名称/版本/依赖/入口模块
├── src/*.sin         # 库源码（Sincoding 写的）
├── blocks.json       # 积木外观元数据（可选）：中文名/分类/颜色/图标
└── native/           # 可选：C/ELF 原生部分，走既有 JSON 桥（§DESIGN 4.4）
```

**3）积木自动生成（关键体验）**

- **函数签名 → 积木**由引擎自动完成：参数即槽位、类型决定槽形状、
  有返回值即 reporter 积木——导入一个库，调色板**自动多出一个分类**。
- `blocks.json` 只管「外观」：中文显示名、分类、颜色；**不写也能用**
  （默认英文函数名积木）。签名变了积木自动跟着变，元数据永不驱动语义。
- IDE 增加「库管理」面板：导入/移除 `.sinlib`，随项目 `.sinproj` 一起保存依赖。

**4）标准库 v0 规划**（全部 `.sin`，M2 之后可写）

| 模块 | 内容 |
|---|---|
| `std/mathx` | `clamp / lerp / sign / dist`（`sqrt/sin/cos` 等走 extern 进 prelude） |
| `std/arrayx` | 排序、查找、求和/最值、填充 |
| `std/collections` | 定长数组上的栈 / 队列 / 环形缓冲 |
| `algo`（示例库） | BFS 网格寻路、简单碰撞扫描——作为「导入算法库」的对外演示 |

### D6 调试器双形态：编辑期在 Web，成品期用 imgui

| 形态 | 场景 | 技术 |
|---|---|---|
| **编辑期调试** | IDE 内边写边调 | interp.js：断点（右键积木）、单步、调用栈、变量面板，当前积木发光 |
| **成品调试** | 编出来的原生程序里调 | **imgui overlay**（rlImGui + raylib）：debug 构建注入，F12 呼出——变量监视、暂停/逐帧、精灵检查器、日志控制台 |

- imgui 在这里是**最佳工具**：即时模式天生适合每帧重画的监视面板；
  它不适合的只是异形积木编辑（该结论维持不变）。
- 发布构建零开销：debug overlay 由编译开关（`-DSIN_DEBUG`）控制，发布版不链 imgui。
- 两个调试器共享同一套「调试信息」：codegen 在 debug 构建给每条语句注入
  行号/积木 ID 回调，这也是 overlay 能把「当前执行到哪个积木」回传显示的基础。

---

## 3. 里程碑计划

> 每个里程碑独立可验收、可合并；M1 是纯重构（对外行为零变化），其余逐个解锁能力。

> **实施调整（实读代码后）**：M1（递归 TypeRef 纯重构）与 M2 步骤1（数组作参数/返回值）
> 合并推进。实读代码发现「数组作参数/返回值」用现有扁平三元组 `{base,len,structName}`
> 即可实现，**不需要递归**；真正需要递归 TypeRef 的是**嵌套**（结构体数组 / 数组字段）。
> 故按「按需抽象」把递归 TypeRef 推迟到 M2 做嵌套时再引入（届时有新功能验证它），
> 先交付数组自由进出函数。**已完成**：数组作参数/返回值/整体赋值（D2 值语义，C 侧结构体
> 包裹），全链路 parser/checker/codegen/serializer 同步，新增正反测试；`--emit src/blocks`
> 与诊断对现有例子逐字节不变。**待办**：有 emcc 时重编 `editor/sinc.wasm`；下一步做嵌套时引入递归 TypeRef。

### M1 —— 类型系统地基重构（TypeRef）

**目标**：扁平类型 → 递归 TypeRef，全链路替换，**对外行为逐字节不变**。

| 文件 | 改动 |
|---|---|
| `compiler/include/ast.h` | `Type`/`arrayLen`/`structName` → `TypeRef`；节点字段替换 |
| `compiler/src/parser.cpp` | 类型标注解析改为递归（`T[N]`、将来 `T[N][M]`） |
| `compiler/src/type_checker.cpp` | 类型相等/推断/报错信息全部走 TypeRef 递归比较 |
| `compiler/src/codegen.cpp` | C 类型发射改为递归（本期仍维持现有限制，只换表示） |
| `compiler/src/serializer.cpp` | 类型序列化走 TypeRef |
| `compiler/src/wasm_api.cpp` | 积木 JSON 里的类型字段同步 |

**验收**：`tests/run_tests.sh` 全绿；对全部 `examples/*.sin` 做
`sinc --emit src` 与重构前**逐字节 diff 为空**；`--emit blocks` JSON 不变。

### M2 —— 聚合数据自由流动（本方向的核心解锁）

**目标**：数组/结构体像 int 一样进出函数、互相嵌套。

分四小步（每步带测试独立提交）：

1. **数组作参数/返回值/整体赋值**（D2 结构体包裹落地）。
2. **结构体嵌套**：字段可以是另一个结构体。
3. **结构体含数组字段 / 结构体数组**：`Enemy { trail: float[8] }`、`let es: Enemy[32]`。
4. **for-each 语法糖**：`for e in es { ... }`（降解为下标循环，积木侧一个新积木）。

**同步链（每步都要走完，这是本项目的纪律）**：
parser → checker → codegen → serializer → wasm 重编（`tools/build_sinc_wasm.sh`）
→ 调色板/reporter 积木（`editor/app.js`、变量下拉需按新类型过滤）
→ `blockmodel.js` 镜像同步（M3 前的最后几次；之后退役）
→ `interp.js` 预览语义 → `tests/cases/` 新增正反用例 → `LANGUAGE.md` 更新。

**验收**：能写出并编译运行
`fn update(es: Enemy[32], n: int) -> int` 风格的程序；新增 ≥8 个 e2e 用例
（含 4 个类型错误用例：数组长度不匹配、嵌套字段类型错等）。

### M3 —— 引擎查询层（mini-LSP）+ 退役 JS 镜像

**目标**：IDE 从「带积木的编辑器」变成「真 IDE」；序列化收归引擎单份。

1. `wasm_api.cpp` 实现 D4 的查询 API（符号表在 type_checker 里已有雏形，补 def/use 记录）。
2. 编辑器接入：**悬停显示类型** → **Ctrl+点击跳转定义** → **F2 重命名**（作用域安全，
   积木与文本同时更新——AST 唯一真相让这件事天然安全，是最好的对外演示）。
3. `applyEdit` 结构化编辑上线，积木拖拽改走引擎，**删除 `blockmodel.js` 的 modelToSource 镜像**。

**验收**：改名一个被 3 处引用的函数，文本/积木/诊断同步正确；
`blockmodel.js` 镜像代码删除后 `verify_ide.js` 全绿。

### M4 —— 模块系统与库生态（D5 落地）

**目标**：用户可以用 Sincoding 写库、导入库，导入即得可视化积木。

分四小步：

1. **多文件编译 + `import`**：模块解析 / 符号合并 / 重名诊断；
   `.sinproj` 记录依赖；wasm 引擎同步支持多文件（IDE 里库源码只读展示）。
2. **标准库 v0**（`std/mathx`、`std/arrayx`、`std/collections`，全 `.sin` 自写；
   `sqrt/sin/cos/floor` 等 extern 进 prelude）——这是 M2 成果的第一批消费者。
3. **运行时实用性**（跟库一起补齐）：字符串 arena（D3：拼接/比较/`len`/`str(n)`）、
   碰撞 API（`sprite_touching`/`sprite_touch_edge`，runtime + interp 双实现）。
4. **`.sinlib` 导入 + 积木自动生成**：签名 → 积木由引擎完成，`blocks.json` 管外观；
   IDE 库管理面板；做一个 `algo` 示例库（排序 + BFS 寻路）作对外演示。

**验收**：新建项目 → 导入 `algo.sinlib` → 调色板出现「算法」分类 →
拖「BFS 寻路」积木跑通一个网格寻路 demo → 一键发布可玩。

### M5 —— 调试器双形态（D6 落地，杀手级特性）

**编辑期（Web IDE，interp.js 升级）：**

- **断点**：右键积木设断点（积木上出现红点）。
- **单步**：暂停后逐积木执行，当前积木发光（已有）+ 调用栈面板。
- **变量面板**：暂停时显示各作用域变量的值**和类型**。

**成品期（原生 debug 构建，imgui overlay）：**

- codegen 在 `-DSIN_DEBUG` 下给每条语句注入行号/积木 ID 回调。
- rlImGui 接入 runtime：F12 呼出面板——全局/局部变量监视、暂停/逐帧步进、
  精灵检查器（位置/朝向/造型）、日志控制台。
- 发布构建不链 imgui，零开销。

> 「看着自己的程序一块一块地跑」——编辑器里如此，编出来的 exe 里也如此。
> 这是 Scratch 没有、文本 IDE 也做不到这么直观的东西。

### M6 —— 验收样板：用正经结构 + 库生态重写 guardian

- 用 `Enemy` 结构体数组 + `update/draw` 函数重写 `examples/guardian.sin`，
  并**导入 `std/arrayx` 与 `algo` 库**（排序计分榜、寻路敌人）展示库生态。
- 全程只用积木完成一遍（吃自己的狗粮，记录卡点反哺 backlog）。
- 一键发布 Windows/Android/Web/单文件 HTML 四端；Windows 版按 F12 演示 imgui 调试面板。
- 产出一篇「从积木到原生 exe」的图文 walkthrough 放 `docs/`。

---

## 4. 里程碑之外的持续事项

- **测试纪律**：每个语言特性 = 正例 e2e + 反例（类型错）+ serializer 幂等测试
  （`src → AST → src` 两次结果一致）+ 积木往返测试（`blocks → AST → src → AST → blocks`）。
- **发布链健康**：`verify_publish.js` 保持全绿；每个里程碑末在干净环境跑一次
  Windows 交叉编译 + 单文件 HTML 发布。
- **文档同步**：`LANGUAGE.md` 与实现同 PR 更新，不欠账。

## 5. 风险与对策

| 风险 | 对策 |
|---|---|
| M1 重构范围失控 | 严格「表示替换、行为不变」，用逐字节 diff 卡死；不顺手加特性 |
| 双实现漂移（blockmodel/interp） | M3 消灭 blockmodel 镜像；interp 用共享用例表与 runtime 对测 |
| 值语义拷贝性能 | 教学规模不构成问题；出现热点再做「大聚合引用传参」优化，不提前设计 |
| 字符串 arena 生命周期被误用 | 规则写进 LANGUAGE.md + 诊断器对「跨帧持有字符串」给警告（后续） |
| wasm API 膨胀 | 查询 API 统一 JSON in/out，一个入口函数 + op 分发，避免导出面失控 |
| 库积木元数据与签名漂移 | 积木**只由签名生成**，`blocks.json` 仅管外观；签名一变积木自动变，元数据永不驱动语义 |
| 模块重名冲突 | MVP 扁平命名空间 + 显式重名诊断；确有需求再上 `mod.func` 限定语法 |
| imgui overlay 拖累发布体积 | debug/发布双构建配置，发布版不链 imgui（编译开关隔离） |

## 6. 执行顺序总结

```
M1 TypeRef 重构（地基，行为不变）
 └→ M2 聚合数据流动（数组/结构体解锁，核心价值）
     └→ M3 mini-LSP + 退役 JS 镜像（真 IDE + 还清最大技术债）
         └→ M4 模块系统 + Sincoding 自写标准库 + .sinlib 导入即得积木（生态）
             └→ M5 调试器双形态：Web 断点单步 + 原生 imgui overlay（杀手特性）
                 └→ M6 guardian 重写（用上库生态）+ 四端发布（对外验收）
```

第一铲土：**M1**。它不产出新功能，但让之后每一步都踩在干净的类型表示上；
且有逐字节 diff 做安全网，是风险最低的起点。
