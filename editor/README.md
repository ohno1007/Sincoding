# 积木编辑器（前端）

> 路线图阶段 3。核心原则：**AST 是唯一真相源**——积木与文本都由 AST 派生，二者绝不直接互转。

```
文本编辑 ──reparse──► AST ──serializeSource──► 文本
积木拖拽 ──修改────► AST ──serializeBlocks──► 积木(JSON) ──► 渲染
```

## 单页 IDE：`index.html`

仿 Scratch 的积木编辑器，零依赖（纯 HTML/CSS/JS），线性描边图标。

- **多精灵 / 多页积木**：底部精灵栏，每个精灵拥有自己的一页积木（program）与一组造型；切换精灵即切换积木页与造型集。
- **无限积木画布**：拖空白处平移、滚轮缩放，脚本可自由拖动摆放。
- **可编辑积木**：数字 / 变量 / 函数名直接点改，布尔积木点击切换；从调色板加入 `let / if / while / return / print` 积木。
- **实时文本写回**：任何积木编辑都即时更新右侧文本视图，结果与 `sinc --emit src` **逐字节一致**（由 `blockmodel.js` 保证，并有测试交叉验证）。
- **精灵造型画板**：画笔 / 橡皮 / 调色板 / 笔刷大小 / 清空 / 导出 PNG，支持多造型与缩略图。

```bash
tools/render_blocks.sh examples/fib.sin   # 生成 editor/blocks_data.js
# 浏览器打开 editor/index.html
```

底层引擎：

- **AST → 文本**（`sinc --emit src`）：规范化序列化回源码，幂等且语义不变。
- **AST → 积木模型**（`sinc --emit blocks`）：导出积木 JSON，前端据此渲染。
- `blockmodel.js` 的 `modelToSource` 镜像 C++ `serializeSource`，保证积木编辑的写回与规范引擎对齐。
- `block_viewer.html`：只读积木查看器（轻量，供截图测试）。

`blocks_data.js` 为生成文件（默认内置 `fib` 示例）。

## 后续

- 文本方向 reparse 走 `sinc --emit blocks`（规范引擎），语法错时显示"部分有效"积木状态。
- 拖拽积木在脚本间重排 / 嵌入控制块的"嘴巴"。
- 按设计最终落地为原生 imgui + Qt 外壳；此 HTML IDE 是交互与渲染模型的原型。
