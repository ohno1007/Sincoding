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
- **积木拖拽重排**：按住语句积木拖动，可在各 stack（函数体 / 控制块嘴巴）间移动；落点高亮提示，落下即改 AST 并刷新文本。
- **数据/控制积木**：调色板含 设变量 / 设字符串 / 设数组 / 数组赋值 `a[i]=v` / `if` / `while` / `for i in a..b` 等，点击即加入选中脚本。
- **舞台**：精灵以其当前造型为外观/纹理（画板里画什么，舞台上精灵就长什么样），可拖动摆位。
- **实时编辑预览**（`interp.js`）：内置积木解释器，直接在浏览器跑当前精灵程序，编辑即重跑（类 Vue）；
  支持键盘事件、广播、声音（Web Audio）。游戏主循环 `while stage_running()` 被逐帧（rAF）执行以不阻塞。
- **事件 / 声音积木**：调色板含 当按键/广播/收到/播放音调 等（对应运行时 `broadcast`/`received`/`play_tone`）。
- **多精灵并行**：预览把所有精灵的程序作为 actor 并行跑在同一舞台（共享世界、每帧只清一次屏）。
- **项目级共享状态**：结构体 / 全局变量 / 数组属于整个项目，所有精灵共享同一组；任一精灵里
  增删全局，其它精灵立即可见，预览里各 actor 的环境栈底指向同一个共享作用域（一改全见）。
- **语法高亮**：文本框为透明 textarea 覆盖在高亮层之上，关键字/类型/字符串/数字/注释着色。
- **一键导出**：「导出 .sin」自动补全运行时 `extern` 声明，产出可被 `build_native.sh` / `build_web.sh` 直接编译的源码。
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
- **文本 → 积木（反向）**：把编译器编成 WebAssembly（`tools/build_sinc_wasm.sh` → `editor/sinc.{js,wasm}`），
  浏览器内直接调用规范引擎解析文本框内容，实时重建积木；语法/类型错时容错返回部分积木。
- `block_viewer.html`：只读积木查看器（轻量，供截图测试）。

`blocks_data.js` 为生成文件（默认内置 `fib` 示例）。

## 后续

- 文本方向 reparse 走 `sinc --emit blocks`（规范引擎），语法错时显示"部分有效"积木状态。
- 拖拽积木在脚本间重排 / 嵌入控制块的"嘴巴"。
- 按设计最终落地为原生 imgui + Qt 外壳；此 HTML IDE 是交互与渲染模型的原型。
