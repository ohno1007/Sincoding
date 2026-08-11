#!/usr/bin/env bash
# toolchains.sh — 工具链自动发现与激活（被各 build_*.sh / 测试 source）
#
# 目标：**用户不需要自己准备环境**。执行一次
#     tools/setup_toolchains.sh
# 之后，本文件负责把工具链找出来并激活：
#   · emsdk 自动进 PATH（不必再手动 source emsdk_env.sh）
#   · NDK / Android SDK 路径自动导出（不必再 export ANDROID_NDK）
#   · 各平台 raylib 静态库路径自动解析（native / web / win / android）
#
# 查找顺序：环境变量（用户显式指定，最高优先） → 仓库内工具链目录
# （.toolchains/，setup 默认装这里） → 系统位置（/usr/local、/usr/lib）。
# 因此系统里已装好的工具链同样能直接用，不会被强制重装。

[[ -n "${SIN_TOOLCHAINS_SOURCED:-}" ]] && return 0
SIN_TOOLCHAINS_SOURCED=1

SIN_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIN_TC="${SIN_TOOLCHAIN_DIR:-$SIN_ROOT/.toolchains}"
export SIN_ROOT SIN_TC

# setup 生成的记录（已安装/已发现的路径），若有则先加载
[[ -f "$SIN_TC/env.sh" ]] && . "$SIN_TC/env.sh"

# 取第一个存在的文件 / 目录；都不存在则返回空（不报错，交调用方判断）
sin_pick_file() { local p; for p in "$@"; do [[ -f "$p" ]] && { printf '%s' "$p"; return 0; }; done; return 0; }
sin_pick_dir()  { local p; for p in "$@"; do [[ -d "$p" ]] && { printf '%s' "$p"; return 0; }; done; return 0; }

# ---- 各平台 raylib 静态库 / 头文件 ----
ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"

RAYLIB_LIB="${RAYLIB_LIB:-$(sin_pick_file "$SIN_TC/lib/native/libraylib.a" /usr/local/lib/libraylib.a)}"
RAYLIB_WEB_LIB="${RAYLIB_WEB_LIB:-$(sin_pick_file "$SIN_TC/lib/web/libraylib.a" /usr/local/lib/web/libraylib.a)}"
RAYLIB_WIN_LIB="${RAYLIB_WIN_LIB:-$(sin_pick_file "$SIN_TC/lib/win/libraylib.a" /usr/local/lib/win/libraylib.a)}"
RAYLIB_ANDROID_LIB="${RAYLIB_ANDROID_LIB:-$(sin_pick_file \
    "$SIN_TC/lib/android/$ANDROID_ABI/libraylib.a" "/usr/local/lib/android/$ANDROID_ABI/libraylib.a")}"

RAYLIB_INCLUDE="${RAYLIB_INCLUDE:-$(sin_pick_dir "$SIN_TC/include" /usr/local/include)}"
RAYLIB_WEB_INCLUDE="${RAYLIB_WEB_INCLUDE:-$RAYLIB_INCLUDE}"
RAYLIB_WIN_INCLUDE="${RAYLIB_WIN_INCLUDE:-$RAYLIB_INCLUDE}"
RAYLIB_ANDROID_INCLUDE="${RAYLIB_ANDROID_INCLUDE:-$RAYLIB_INCLUDE}"
export RAYLIB_LIB RAYLIB_WEB_LIB RAYLIB_WIN_LIB RAYLIB_ANDROID_LIB
export RAYLIB_INCLUDE RAYLIB_WEB_INCLUDE RAYLIB_WIN_INCLUDE RAYLIB_ANDROID_INCLUDE ANDROID_ABI

# ---- Android NDK / SDK ----
ANDROID_NDK="${ANDROID_NDK:-$(sin_pick_dir "$SIN_TC/android-ndk" /usr/lib/android-ndk "$HOME/android-ndk")}"
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$(sin_pick_dir "$SIN_TC/android-sdk" /usr/lib/android-sdk "$HOME/android-sdk")}}"
export ANDROID_NDK ANDROID_SDK_ROOT
[[ -n "$ANDROID_SDK_ROOT" ]] && export ANDROID_HOME="$ANDROID_SDK_ROOT"

# ---- emsdk：需要时自动激活进 PATH ----
SIN_EMSDK="${SIN_EMSDK:-$(sin_pick_dir "$SIN_TC/emsdk" "$HOME/emsdk" /opt/emsdk)}"
export SIN_EMSDK

# 调用后 emcc/emcmake 可直接使用（已在 PATH 则原样返回）
sin_activate_emsdk() {
    command -v emcc >/dev/null 2>&1 && return 0
    [[ -n "$SIN_EMSDK" && -f "$SIN_EMSDK/emsdk_env.sh" ]] || return 1
    # emsdk_env.sh 有未定义变量引用，临时关掉 -u/-e 保护
    local had_u=0 had_e=0
    [[ $- == *u* ]] && had_u=1 && set +u
    [[ $- == *e* ]] && had_e=1 && set +e
    . "$SIN_EMSDK/emsdk_env.sh" >/dev/null 2>&1
    [[ $had_u == 1 ]] && set -u
    [[ $had_e == 1 ]] && set -e
    command -v emcc >/dev/null 2>&1
}

# ---- 各目标是否就绪（供构建脚本与测试统一判断）----
sin_have_native()  { [[ -n "$RAYLIB_LIB" ]] || pkg-config --exists raylib 2>/dev/null; }
sin_have_web()     { [[ -n "$RAYLIB_WEB_LIB" ]] && sin_activate_emsdk; }
sin_have_windows() { [[ -n "$RAYLIB_WIN_LIB" ]] && command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; }
sin_have_android() {
    [[ -n "$RAYLIB_ANDROID_LIB" && -n "$ANDROID_NDK" ]] &&
    [[ -x "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang" ]]
}
# imgui（原生成品的 F12 调试面板）：源码直接参与编译，无需预编译库
SIN_IMGUI_DIR="${SIN_IMGUI_DIR:-$(sin_pick_dir "$SIN_TC/src/imgui")}"
SIN_RLIMGUI_DIR="${SIN_RLIMGUI_DIR:-$(sin_pick_dir "$SIN_TC/src/rlImGui")}"
export SIN_IMGUI_DIR SIN_RLIMGUI_DIR
sin_have_imgui() {
    [[ -n "$SIN_IMGUI_DIR" && -f "$SIN_IMGUI_DIR/imgui.cpp" ]] &&
    [[ -n "$SIN_RLIMGUI_DIR" && -f "$SIN_RLIMGUI_DIR/rlImGui.cpp" ]]
}
sin_have_apk() {
    sin_have_android || return 1
    local bt="$ANDROID_SDK_ROOT/build-tools/${ANDROID_BUILD_TOOLS:-34.0.0}"
    [[ -x "$bt/aapt2" && -x "$bt/apksigner" ]] &&
    [[ -f "$ANDROID_SDK_ROOT/platforms/${ANDROID_PLATFORM:-android-29}/android.jar" ]]
}

# 缺工具链时给出可执行的补救提示（而不是让用户自己查文档）
sin_hint() { echo "缺少 $1 工具链。请运行： tools/setup_toolchains.sh $2" >&2; }
