# Sincoding 桌面版 IDE

把整个网页积木编辑器（`editor/`）打包成**单文件可执行程序**：双击即用，
无需安装、无需联网。启动后在本机起一个静态服务并自动打开浏览器显示 IDE，
关闭黑色命令行窗口即退出。

积木 ⇄ 代码双向同步、实时预览、控制台、造型画板、保存/打开项目（`.sinproj`）
全部在本地浏览器里运行；文本→积木的反向解析由内置的 `sinc.wasm`（编译器编成的
WebAssembly）完成，所以**完全离线可用**。

## 用法（Windows）

1. 双击 `Sincoding-IDE.exe`。
2. 会弹出一个黑色命令行窗口并自动打开默认浏览器，显示 IDE。
   - 若没自动弹出，手动在浏览器访问窗口里打印的地址（形如 `http://127.0.0.1:<端口>/index.html`）。
3. 关闭那个黑色窗口即退出 IDE。

> 「发布」按钮：桌面版未内置交叉编译工具链，点发布会自动**降级为下载 `.sin` 源码**；
> 要真正编出各平台成品，请在装有工具链的机器上用 `tools/build_*.sh` 或 `tools/ide_server.py`。

## 构建

用 Go（`CGO_ENABLED=0` 纯静态，`go:embed` 把 `editor/` 整目录嵌进二进制）交叉编译：

```bash
tools/build_desktop_ide.sh windows   # 产出 dist/Sincoding-IDE-windows.exe
tools/build_desktop_ide.sh linux     # 产出 dist/Sincoding-IDE-linux
tools/build_desktop_ide.sh darwin    # 产出 dist/Sincoding-IDE-darwin（arm64）
```

`desktop/web/` 是构建时从 `editor/` 拷来的暂存目录（已 gitignore）。
