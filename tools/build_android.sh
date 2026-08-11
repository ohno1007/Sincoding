#!/usr/bin/env bash
# build_android.sh — 把一个 Sincoding 程序交叉编成 Android 原生库 libsincoding.so
#
#   tools/build_android.sh [--debug] <input.sin> <out_so>
#
#   --debug  编出带调试面板的库（imgui overlay，点左下角开关；与桌面 F12 同源）
#
# 用 Android NDK 的 clang，把 sinc 生成的 C + runtime + raylib(Android)
# 链成一个 NativeActivity 共享库（导出 ANativeActivity_onCreate，由系统调用，
# 进而调用程序的 main）。该 .so 放进 APK 的 jniLibs/<abi>/ 即可打包。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/tools/toolchains.sh"      # 自动发现 NDK / raylib-android（无需用户 export）
SINC="$ROOT/compiler/build/sinc"
ABI="${ANDROID_ABI:-arm64-v8a}"
API="${ANDROID_API:-29}"

DEBUG=0
if [[ "${1:-}" == "--debug" ]]; then DEBUG=1; shift; fi
if [[ $# -ne 2 ]]; then echo "用法: $0 [--debug] <input.sin> <out_so>" >&2; exit 2; fi
SRC="$1"; OUT="$2"

[[ -x "$SINC" ]] || { echo "找不到 sinc，请先构建 compiler" >&2; exit 1; }
[[ -n "$ANDROID_NDK" && -d "$ANDROID_NDK" ]] || { sin_hint "Android NDK" android; exit 1; }
[[ -n "$RAYLIB_ANDROID_LIB" && -f "$RAYLIB_ANDROID_LIB" ]] || { sin_hint "raylib(android)" android; exit 1; }

case "$ABI" in
    arm64-v8a)   TARGET=aarch64-linux-android ;;
    armeabi-v7a) TARGET=armv7a-linux-androideabi ;;
    x86_64)      TARGET=x86_64-linux-android ;;
    *) echo "未知 ABI: $ABI" >&2; exit 1 ;;
esac
TOOLCHAIN="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64"
CC="$TOOLCHAIN/bin/${TARGET}${API}-clang"
[[ -x "$CC" ]] || { echo "找不到 NDK clang: $CC" >&2; exit 1; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
echo "[1/2] 转译 $SRC → C"
if [[ "$DEBUG" == "1" ]]; then "$SINC" "$SRC" --debug -o "$TMP/program.c"
else "$SINC" "$SRC" -o "$TMP/program.c"; fi

mkdir -p "$(dirname "$OUT")"
COMMON_INC=(-I"$ROOT/runtime" -I"$RAYLIB_ANDROID_INCLUDE"
            -I"$ANDROID_NDK/sources/android/native_app_glue")
if [[ "$DEBUG" == "1" ]]; then
    sin_have_imgui || { sin_hint "Dear ImGui" imgui; exit 1; }
    CXX="$TOOLCHAIN/bin/${TARGET}${API}-clang++"
    [[ -x "$CXX" ]] || { echo "找不到 NDK clang++: $CXX" >&2; exit 1; }
    echo "[2/2] NDK 链接（含调试面板）→ $OUT （$ABI, API $API）"
    # C 部分按 C 编，imgui/rlImGui/overlay 是 C++ —— 分别编译再用 clang++ 收口
    for c in "$TMP/program.c" "$ROOT/runtime/prelude.c" "$ROOT/runtime/runtime.c"; do
        "$CC" -c -fPIC -std=c11 -O2 -DSIN_DEBUG "${COMMON_INC[@]}" \
            "$c" -o "$TMP/$(basename "$c" .c).o"
    done
    IMGUI_SRC=("$SIN_IMGUI_DIR"/imgui.cpp "$SIN_IMGUI_DIR"/imgui_draw.cpp
               "$SIN_IMGUI_DIR"/imgui_tables.cpp "$SIN_IMGUI_DIR"/imgui_widgets.cpp
               "$SIN_RLIMGUI_DIR"/rlImGui.cpp "$ROOT/runtime/debug_overlay.cpp")
    CXX_OBJS=()
    for cc in "${IMGUI_SRC[@]}"; do
        o="$TMP/cxx_$(basename "$cc" .cpp).o"
        "$CXX" -c -fPIC -std=c++17 -O2 -DSIN_DEBUG "${COMMON_INC[@]}" \
            -I"$SIN_IMGUI_DIR" -I"$SIN_RLIMGUI_DIR" "$cc" -o "$o"
        CXX_OBJS+=("$o")
    done
    "$CXX" -shared -fPIC \
        "$TMP/program.o" "$TMP/prelude.o" "$TMP/runtime.o" "${CXX_OBJS[@]}" \
        -u ANativeActivity_onCreate -Wl,--wrap=fopen \
        "$RAYLIB_ANDROID_LIB" \
        -static-libstdc++ \
        -landroid -llog -lEGL -lGLESv2 -lOpenSLES -lm \
        -o "$OUT"
else
    echo "[2/2] NDK 链接 → $OUT （$ABI, API $API）"
    # -u 强制从 raylib 静态库拉入 NativeActivity 入口
    "$CC" -shared -fPIC -std=c11 -O2 \
        "${COMMON_INC[@]}" \
        "$TMP/program.c" "$ROOT/runtime/prelude.c" "$ROOT/runtime/runtime.c" \
        -u ANativeActivity_onCreate -Wl,--wrap=fopen \
        "$RAYLIB_ANDROID_LIB" \
        -landroid -llog -lEGL -lGLESv2 -lOpenSLES -lm \
        -o "$OUT"
fi
echo "完成: $OUT"
file "$OUT" | sed 's/^[^:]*: //'
