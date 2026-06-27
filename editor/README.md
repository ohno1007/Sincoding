# 积木编辑器（前端）

> 路线图阶段 3。核心原则：**AST 是唯一真相源**——积木与文本都由 AST 派生，二者绝不直接互转。

```
文本编辑 ──reparse──► AST ──serializeSource──► 文本
积木拖拽 ──修改────► AST ──serializeBlocks──► 积木(JSON) ──► 渲染
```

当前已实现：

- **AST → 文本**（`sinc --emit src`）：把 AST 规范化序列化回 Sincoding 源码，幂等且语义不变。
- **AST → 积木模型**（`sinc --emit blocks`）：把 AST 导出为积木 JSON，供前端渲染。
- **积木查看器** `block_viewer.html`：零依赖的 Scratch 风格渲染器，把积木 JSON 画成嵌套积木。

## 本地查看

```bash
# 由某个 .sin 生成积木数据并写入 editor/blocks_data.js
tools/render_blocks.sh examples/fib.sin
# 然后用浏览器打开 editor/block_viewer.html
```

无显示器环境可直接截图：

```bash
tools/render_blocks.sh examples/fib.sin out.png   # 需要 node + playwright
```

`blocks_data.js` 为生成文件（默认内置 `fib` 示例）。

## 后续

- 拖拽编辑积木 → 修改 AST → 回写文本（双向同步的"写"方向）
- 文本语法错误时显示"部分有效"积木状态
- 按设计最终落地为原生 imgui + Qt 外壳；此 HTML 查看器是渲染模型的轻量原型
