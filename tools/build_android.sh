#!/usr/bin/env bash
# build_android.sh — 把一个 Sincoding 程序交叉编成 Android 原生库 libsincoding.so
#
#   tools/build_android.sh <input.sin> <out_so>
#
# 用 Android NDK 的 clang，把 sinc 生成的 C + runtime + raylib(Android)
# 链成一个 NativeActivity 共享库（导出 ANativeActivity_onCreate，由系统调用，
# 进而调用程序的 main）。该 .so 放进 APK 的 jniLibs/<abi>/ 即可打包。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SINC="$ROOT/compiler/build/sinc"
ANDROID_NDK="${ANDROID_NDK:-/usr/lib/android-ndk}"
ABI="${ANDROID_ABI:-arm64-v8a}"
API="${ANDROID_API:-29}"
RAYLIB_ANDROID_LIB="${RAYLIB_ANDROID_LIB:-/usr/local/lib/android/$ABI/libraylib.a}"
RAYLIB_ANDROID_INCLUDE="${RAYLIB_ANDROID_INCLUDE:-/usr/local/include}"

if [[ $# -ne 2 ]]; then echo "用法: $0 <input.sin> <out_so>" >&2; exit 2; fi
SRC="$1"; OUT="$2"

[[ -x "$SINC" ]] || { echo "找不到 sinc，请先构建 compiler" >&2; exit 1; }
[[ -d "$ANDROID_NDK" ]] || { echo "找不到 NDK: $ANDROID_NDK（设 ANDROID_NDK）" >&2; exit 1; }
[[ -f "$RAYLIB_ANDROID_LIB" ]] || { echo "缺少 raylib Android 静态库: $RAYLIB_ANDROID_LIB" >&2; exit 1; }

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
"$SINC" "$SRC" -o "$TMP/program.c"

echo "[2/2] NDK 链接 → $OUT （$ABI, API $API）"
mkdir -p "$(dirname "$OUT")"
# -u 强制从 raylib 静态库拉入 NativeActivity 入口
"$CC" -shared -fPIC -std=c11 -O2 \
    -I"$ROOT/runtime" -I"$RAYLIB_ANDROID_INCLUDE" \
    -I"$ANDROID_NDK/sources/android/native_app_glue" \
    "$TMP/program.c" "$ROOT/runtime/prelude.c" "$ROOT/runtime/runtime.c" \
    -u ANativeActivity_onCreate \
    "$RAYLIB_ANDROID_LIB" \
    -landroid -llog -lEGL -lGLESv2 -lOpenSLES -lm \
    -o "$OUT"
echo "完成: $OUT"
file "$OUT" | sed 's/^[^:]*: //'
