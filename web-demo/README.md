# Web Demo — 接球小游戏（WebAssembly）

`examples/catch.sin` 经 Sincoding 转 C、emscripten 编成 WebAssembly 的成品。
方向键左右移动挡板接住下落的小球。

## 运行（wasm 不支持 file://，需 HTTP 服务器）

```bash
python3 -m http.server 8000 --directory web-demo
# 浏览器访问 http://localhost:8000  （点击画面后用 ← → 控制）
```

## 重新生成

```bash
tools/build_web.sh examples/catch.sin web-demo
```

> 本目录的 index.html/.js/.wasm 为生成产物（由上面的命令产出）。
