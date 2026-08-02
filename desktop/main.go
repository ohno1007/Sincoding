// Sincoding 桌面版 IDE —— 单文件可执行程序
//
// 把整个网页积木编辑器（editor/）打进二进制里，启动后在本机起一个静态服务并自动
// 打开浏览器显示 IDE。无需安装、无需联网：积木/代码双向同步、实时预览、控制台、
// 造型画板、保存/打开项目全部在本地浏览器里跑（反向解析用内置的 sinc.wasm）。
//
// 构建（在 Linux 上交叉编译出 Windows exe）：
//   tools/build_desktop_ide.sh        # 见该脚本
package main

import (
	"embed"
	"fmt"
	"io/fs"
	"log"
	"mime"
	"net"
	"net/http"
	"os/exec"
	"runtime"
	"time"
)

// 编译期把 web/（= editor/ 的拷贝）整目录嵌入二进制
//
//go:embed all:web
var webFS embed.FS

func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	case "darwin":
		cmd = exec.Command("open", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	_ = cmd.Start()
}

func main() {
	// 正确的 MIME，尤其 .wasm（浏览器要求 application/wasm 才能流式实例化）
	_ = mime.AddExtensionType(".wasm", "application/wasm")
	_ = mime.AddExtensionType(".js", "text/javascript")
	_ = mime.AddExtensionType(".css", "text/css")

	sub, err := fs.Sub(webFS, "web")
	if err != nil {
		log.Fatal(err)
	}

	mux := http.NewServeMux()
	mux.Handle("/", http.FileServer(http.FS(sub)))
	// 发布「单文件 HTML」完全在浏览器内生成，免依赖，不经此接口；只有选了 wasm/原生/安卓
	// 才会请求这里——桌面版没内置交叉编译工具链，返回 501，前端自动降级为下载 .sin 源码。
	mux.HandleFunc("/api/publish", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusNotImplemented)
		_, _ = w.Write([]byte(`{"ok":false,"error":"桌面版只内置「单文件 HTML」免依赖发布；wasm/原生/安卓需在装有工具链的机器上用 tools/ide_server.py 或 tools/build_*.sh"}`))
	})

	ln, err := net.Listen("tcp", "127.0.0.1:0") // 自动选空闲端口
	if err != nil {
		log.Fatal(err)
	}
	port := ln.Addr().(*net.TCPAddr).Port
	url := fmt.Sprintf("http://127.0.0.1:%d/index.html", port)

	fmt.Println("====================================================")
	fmt.Println("  Sincoding 图形化编程 IDE（桌面版）")
	fmt.Println("  正在打开浏览器：", url)
	fmt.Println("  若没自动弹出，请手动在浏览器访问上面的地址。")
	fmt.Println("  关闭此黑色窗口即退出 IDE。")
	fmt.Println("====================================================")

	go func() { time.Sleep(700 * time.Millisecond); openBrowser(url) }()
	log.Fatal(http.Serve(ln, mux))
}
