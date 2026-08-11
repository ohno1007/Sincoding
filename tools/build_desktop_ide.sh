#!/usr/bin/env bash
# build_desktop_ide.sh —— 把网页积木编辑器打包成单文件桌面 IDE 可执行程序
#
#   tools/build_desktop_ide.sh [windows|linux|darwin] [out_path]
#
# 用 Go（CGO_ENABLED=0，纯静态）交叉编译：把 editor/ 整目录嵌入二进制，
# 运行后本机起静态服务 + 自动开浏览器显示 IDE。Windows 产物为单文件 .exe，
# 双击即用、无需安装、无需联网。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OS="${1:-windows}"
DEFAULT_OUT="$ROOT/dist/Sincoding-IDE-$OS"
[[ "$OS" == "windows" ]] && DEFAULT_OUT="$DEFAULT_OUT.exe"
OUT="${2:-$DEFAULT_OUT}"
[[ "$OUT" = /* ]] || OUT="$PWD/$OUT"   # go build 在 desktop/ 里跑，相对路径先钉死到调用方目录

command -v go >/dev/null 2>&1 || { echo "需要 Go 工具链（go 不在 PATH）" >&2; exit 1; }

# 1) 暂存 editor/ → desktop/web/（go:embed 需文件在模块内）
STAGE="$ROOT/desktop/web"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp -r "$ROOT/editor/." "$STAGE/"
# 不打包构建产物 / 体积无用文件
rm -rf "$STAGE/dist" "$STAGE/web" 2>/dev/null || true

# 2) 交叉编译
mkdir -p "$(dirname "$OUT")"
case "$OS" in
	windows) GOOS=windows; GOARCH=amd64 ;;
	linux)   GOOS=linux;   GOARCH=amd64 ;;
	darwin)  GOOS=darwin;  GOARCH=arm64 ;;
	*) echo "未知平台: $OS（windows|linux|darwin）" >&2; exit 1 ;;
esac

echo "[1/2] 嵌入 editor/ → 交叉编译（$GOOS/$GOARCH）"
( cd "$ROOT/desktop" && CGO_ENABLED=0 GOOS="$GOOS" GOARCH="$GOARCH" \
	go build -trimpath -ldflags "-s -w" -o "$OUT" . )

echo "[2/2] 完成"
ls -la "$OUT"
file "$OUT" | sed 's/^[^:]*: //'
