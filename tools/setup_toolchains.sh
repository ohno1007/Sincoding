#!/usr/bin/env bash
# setup_toolchains.sh — 一条命令装齐所有平台的构建工具链
#
#   tools/setup_toolchains.sh              # 装齐全部（native web windows android sdk）
#   tools/setup_toolchains.sh web windows  # 只装指定目标
#   tools/setup_toolchains.sh --check      # 只检测并刷新记录，不安装
#
# 设计原则：**用户不该自己去准备环境**。
#   · 已就绪的目标自动跳过（系统里装过的也认，不重复装）
#   · 全部装进仓库内的 .toolchains/（不污染系统；apt 系统包除外）
#   · 装完写入 .toolchains/env.sh，之后所有 build_*.sh 自动发现，
#     不需要用户再 source emsdk_env.sh 或 export ANDROID_NDK
#
# 目标：
#   native   Linux 原生成品        (raylib + X11/GL 开发包)
#   web      Web(wasm) 成品        (emsdk + raylib-web)
#   windows  Windows .exe          (MinGW-w64 + raylib-win)
#   android  Android .so           (NDK + raylib-android)
#   sdk      可安装 APK            (JDK + Android SDK build-tools；含 android)
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SIN_TC="${SIN_TOOLCHAIN_DIR:-$ROOT/.toolchains}"
SRC_DIR="$SIN_TC/src"
RAYLIB_TAG="${RAYLIB_TAG:-master}"
NDK_VER="${NDK_VER:-r26d}"
BUILD_TOOLS_VER="${ANDROID_BUILD_TOOLS:-34.0.0}"
PLATFORM_VER="${ANDROID_PLATFORM:-android-29}"
ANDROID_API="${ANDROID_API:-29}"
JOBS="$(nproc 2>/dev/null || echo 4)"

CHECK_ONLY=0
TARGETS=()
for a in "$@"; do
    case "$a" in
        --check) CHECK_ONLY=1 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) TARGETS+=("$a") ;;
    esac
done
[[ ${#TARGETS[@]} -eq 0 ]] && TARGETS=(native web windows android sdk)

want() { local t; for t in "${TARGETS[@]}"; do [[ "$t" == "$1" ]] && return 0; done; return 1; }
say()  { echo "[$1] ${*:2}"; }

# ---- root / sudo（仅 apt 系统包需要）----
SUDO=""
if [[ "$(id -u)" != "0" ]]; then command -v sudo >/dev/null 2>&1 && SUDO="sudo"; fi
apt_install() {
    command -v apt-get >/dev/null 2>&1 || { say deps "非 apt 系统，请自行安装: $*"; return 1; }
    if [[ "$(id -u)" != "0" && -z "$SUDO" ]]; then
        say deps "需要 root 安装系统包（$*），请用 sudo 重跑本脚本"; return 1
    fi
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get update -qq >/dev/null 2>&1
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$@" >/dev/null 2>&1
}

mkdir -p "$SIN_TC" "$SRC_DIR"

# raylib 源码（各平台共用一份）
ensure_raylib_src() {
    [[ -d "$SRC_DIR/raylib/src" ]] && return 0
    say raylib "获取源码…"
    command -v git >/dev/null 2>&1 || apt_install git || return 1
    rm -rf "$SRC_DIR/raylib"
    git clone --depth 1 -b "$RAYLIB_TAG" https://github.com/raysan5/raylib.git "$SRC_DIR/raylib" >/dev/null 2>&1
}
# 统一的 raylib 构建：build_raylib <构建子目录> <安装到的 lib 目录> <额外 cmake 参数…>
build_raylib() {
    local bdir="$1" outdir="$2"; shift 2
    ensure_raylib_src || return 1
    ( cd "$SRC_DIR/raylib" && cmake -S . -B "$bdir" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF "$@" >/dev/null 2>&1 &&
      cmake --build "$bdir" -j"$JOBS" >/dev/null 2>&1 ) || return 1
    mkdir -p "$outdir" "$SIN_TC/include"
    find "$SRC_DIR/raylib/$bdir" -name libraylib.a -exec cp {} "$outdir/" \; 2>/dev/null
    cp "$SRC_DIR/raylib/src/raylib.h" "$SRC_DIR/raylib/src/raymath.h" "$SIN_TC/include/" 2>/dev/null
    [[ -f "$outdir/libraylib.a" ]]
}

# ---------------- native ----------------
setup_native() {
    say native "安装 X11/GL 开发包…"
    apt_install build-essential cmake pkg-config libgl1-mesa-dev libx11-dev \
                libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev
    say native "构建 raylib（桌面）…"
    build_raylib build-native "$SIN_TC/lib/native" -DPLATFORM=Desktop
}

# ---------------- web ----------------
setup_web() {
    if [[ ! -d "$SIN_TC/emsdk" ]]; then
        say web "安装 emsdk…"
        git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$SIN_TC/emsdk" >/dev/null 2>&1 || return 1
        ( cd "$SIN_TC/emsdk" && ./emsdk install latest >/dev/null 2>&1 && ./emsdk activate latest >/dev/null 2>&1 ) || return 1
    fi
    export SIN_EMSDK="$SIN_TC/emsdk"
    . "$ROOT/tools/toolchains.sh"
    sin_activate_emsdk || { say web "emsdk 激活失败"; return 1; }
    say web "用 emscripten 构建 raylib（wasm）…"
    ( emcmake cmake -S "$SRC_DIR/raylib" -B "$SRC_DIR/raylib/build-web" -DPLATFORM=Web \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF >/dev/null 2>&1 &&
      cmake --build "$SRC_DIR/raylib/build-web" -j"$JOBS" >/dev/null 2>&1 ) || return 1
    mkdir -p "$SIN_TC/lib/web" "$SIN_TC/include"
    find "$SRC_DIR/raylib/build-web" -name libraylib.a -exec cp {} "$SIN_TC/lib/web/" \;
    cp "$SRC_DIR/raylib/src/raylib.h" "$SRC_DIR/raylib/src/raymath.h" "$SIN_TC/include/" 2>/dev/null
    [[ -f "$SIN_TC/lib/web/libraylib.a" ]]
}

# ---------------- windows ----------------
setup_windows() {
    command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 || { say windows "安装 MinGW-w64…"; apt_install mingw-w64; }
    command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 || { say windows "MinGW 不可用"; return 1; }
    say windows "交叉编译 raylib（Windows）…"
    build_raylib build-win "$SIN_TC/lib/win" -DPLATFORM=Desktop \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/templates/windows/mingw-toolchain.cmake"
}

# ---------------- android（.so）----------------
setup_android() {
    if [[ ! -x "$SIN_TC/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android${ANDROID_API}-clang" ]]; then
        say android "下载 Android NDK $NDK_VER（约 600MB）…"
        command -v unzip >/dev/null 2>&1 || apt_install unzip
        local zip="$SRC_DIR/ndk-$NDK_VER.zip"
        [[ -f "$zip" ]] || curl -fsSL --max-time 1800 -o "$zip" \
            "https://dl.google.com/android/repository/android-ndk-$NDK_VER-linux.zip" || return 1
        say android "解压 NDK…"
        rm -rf "$SIN_TC/android-ndk" "$SRC_DIR/android-ndk-$NDK_VER"
        unzip -q "$zip" -d "$SRC_DIR" || return 1
        mv "$SRC_DIR/android-ndk-$NDK_VER" "$SIN_TC/android-ndk"
    fi
    export ANDROID_NDK="$SIN_TC/android-ndk"
    say android "交叉编译 raylib（$ANDROID_ABI_DEF）…"
    build_raylib build-android "$SIN_TC/lib/android/${ANDROID_ABI_DEF}" -DPLATFORM=Android \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ANDROID_ABI_DEF" -DANDROID_PLATFORM="android-$ANDROID_API"
}

# ---------------- sdk（可安装 APK）----------------
setup_sdk() {
    command -v java >/dev/null 2>&1 || { say sdk "安装 JDK…"; apt_install default-jdk-headless || apt_install openjdk-17-jdk-headless; }
    command -v java >/dev/null 2>&1 || { say sdk "JDK 不可用（sdkmanager 需要 Java）"; return 1; }
    local sdk="$SIN_TC/android-sdk"
    if [[ ! -x "$sdk/cmdline-tools/latest/bin/sdkmanager" ]]; then
        say sdk "下载 Android commandline-tools…"
        command -v unzip >/dev/null 2>&1 || apt_install unzip
        local zip="$SRC_DIR/cmdline-tools.zip"
        [[ -f "$zip" ]] || curl -fsSL --max-time 900 -o "$zip" \
            "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip" || return 1
        rm -rf "$sdk/cmdline-tools"; mkdir -p "$sdk/cmdline-tools"
        unzip -q "$zip" -d "$sdk/cmdline-tools" || return 1
        mv "$sdk/cmdline-tools/cmdline-tools" "$sdk/cmdline-tools/latest"
    fi
    say sdk "安装 build-tools $BUILD_TOOLS_VER 与 $PLATFORM_VER …"
    yes 2>/dev/null | "$sdk/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$sdk" --licenses >/dev/null 2>&1
    "$sdk/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$sdk" \
        "build-tools;$BUILD_TOOLS_VER" "platforms;$PLATFORM_VER" "platform-tools" >/dev/null 2>&1
    [[ -x "$sdk/build-tools/$BUILD_TOOLS_VER/aapt2" ]]
}

ANDROID_ABI_DEF="${ANDROID_ABI:-arm64-v8a}"

# ---- 写记录：让 build_*.sh 无需用户配置即可发现工具链 ----
write_env() {
    . "$ROOT/tools/toolchains.sh" 2>/dev/null || true
    mkdir -p "$SIN_TC"
    {
        echo "# 由 tools/setup_toolchains.sh 生成 —— 记录已安装/已发现的工具链路径。"
        echo "# 各 build_*.sh 经 tools/toolchains.sh 自动加载，用户无需手动配置。"
        [[ -n "${SIN_EMSDK:-}" ]]        && echo "export SIN_EMSDK=\"$SIN_EMSDK\""
        [[ -n "${ANDROID_NDK:-}" ]]      && echo "export ANDROID_NDK=\"$ANDROID_NDK\""
        [[ -n "${ANDROID_SDK_ROOT:-}" ]] && echo "export ANDROID_SDK_ROOT=\"$ANDROID_SDK_ROOT\""
        true
    } > "$SIN_TC/env.sh"
}

# ---- 状态汇总 ----
report() {
    # 重新加载（子 shell 内安装的结果需重新发现）
    unset SIN_TOOLCHAINS_SOURCED
    unset RAYLIB_LIB RAYLIB_WEB_LIB RAYLIB_WIN_LIB RAYLIB_ANDROID_LIB
    . "$ROOT/tools/toolchains.sh"
    echo
    echo "工具链状态（.toolchains/ 或系统位置）："
    local ok=0 miss=0
    for t in native web windows android apk; do
        if "sin_have_$t" >/dev/null 2>&1; then printf '  ✓ %-8s 就绪\n' "$t"; ok=$((ok+1))
        else printf '  ✗ %-8s 缺失 —— tools/setup_toolchains.sh %s\n' "$t" "${t/apk/sdk}"; miss=$((miss+1)); fi
    done
    echo
    echo "就绪 $ok / 缺失 $miss"
}

if [[ $CHECK_ONLY == 1 ]]; then
    write_env
    report
    exit 0
fi

. "$ROOT/tools/toolchains.sh"
echo "工具链安装目录: $SIN_TC"
for t in native web windows android sdk; do
    want "$t" || continue
    case "$t" in
        native)  sin_have_native  && { say native  "已就绪，跳过"; continue; } ;;
        web)     sin_have_web     && { say web     "已就绪，跳过"; continue; } ;;
        windows) sin_have_windows && { say windows "已就绪，跳过"; continue; } ;;
        android) sin_have_android && { say android "已就绪，跳过"; continue; } ;;
        sdk)     sin_have_apk     && { say sdk     "已就绪，跳过"; continue; } ;;
    esac
    if "setup_$t"; then say "$t" "完成 ✓"; else say "$t" "失败 ✗（其余目标继续）"; fi
    write_env
done

write_env
report
