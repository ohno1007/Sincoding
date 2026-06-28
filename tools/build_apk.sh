#!/usr/bin/env bash
# build_apk.sh — 把一个 Sincoding 程序打包成可安装的 Android APK（签名版）
#
#   tools/build_apk.sh <input.sin> <out.apk> [app_label]
#
# 流程：sinc 转 C → NDK 交叉编 libsincoding.so（每个 ABI）→ aapt2 链接出带二进制
# Manifest 的基础 APK → 塞入 jniLibs → zipalign → apksigner 用调试 keystore 签名。
# 产物为可直接 `adb install` 的 APK。NativeActivity + hasCode=false → 无需 dex。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ $# -lt 2 ]]; then echo "用法: $0 <input.sin> <out.apk> [app_label]" >&2; exit 2; fi
SRC="$1"; OUT="$2"; LABEL="${3:-Sincoding}"

# —— 工具链定位（可被环境变量覆盖） ——
ANDROID_NDK="${ANDROID_NDK:-/usr/lib/android-ndk}"
ANDROID_SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/tmp/android-sdk}}"
BUILD_TOOLS_VER="${ANDROID_BUILD_TOOLS:-34.0.0}"
PLATFORM_VER="${ANDROID_PLATFORM:-android-29}"
ABIS="${ANDROID_ABIS:-arm64-v8a}"
MIN_SDK="${ANDROID_MIN_SDK:-24}"
TARGET_SDK="${ANDROID_TARGET_SDK:-29}"
PKG="${ANDROID_PKG:-org.sincoding.game}"

BT="$ANDROID_SDK/build-tools/$BUILD_TOOLS_VER"
AAPT2="$BT/aapt2"
ZIPALIGN="$BT/zipalign"
APKSIGNER="$BT/apksigner"
ANDROID_JAR="$ANDROID_SDK/platforms/$PLATFORM_VER/android.jar"

for t in "$AAPT2" "$ZIPALIGN" "$APKSIGNER" "$ANDROID_JAR"; do
    [[ -e "$t" ]] || { echo "缺少 Android SDK 组件: $t" >&2; exit 1; }
done

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# —— 1) 每个 ABI 交叉编原生库 ——
echo "[1/5] 交叉编译原生库（$ABIS）"
for ABI in $ABIS; do
    mkdir -p "$TMP/lib/$ABI"
    ANDROID_NDK="$ANDROID_NDK" ANDROID_ABI="$ABI" \
        RAYLIB_ANDROID_LIB="/usr/local/lib/android/$ABI/libraylib.a" \
        "$ROOT/tools/build_android.sh" "$SRC" "$TMP/lib/$ABI/libsincoding.so" >/dev/null
    # 缩小体积：strip 调试符号
    "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" \
        "$TMP/lib/$ABI/libsincoding.so" 2>/dev/null || true
done

# —— 2) 生成 Manifest（注入 package / label / SDK 版本） ——
echo "[2/5] 生成 AndroidManifest.xml"
MANIFEST="$TMP/AndroidManifest.xml"
cat > "$MANIFEST" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="$PKG" android:versionCode="1" android:versionName="1.0">
    <uses-sdk android:minSdkVersion="$MIN_SDK" android:targetSdkVersion="$TARGET_SDK" />
    <uses-feature android:glEsVersion="0x00020000" android:required="true" />
    <application android:label="$LABEL" android:hasCode="false"
                 android:allowBackup="false" android:extractNativeLibs="true">
        <activity android:name="android.app.NativeActivity"
                  android:configChanges="orientation|keyboardHidden|screenSize"
                  android:screenOrientation="landscape"
                  android:exported="true">
            <meta-data android:name="android.app.lib_name" android:value="sincoding" />
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
EOF

# —— 3) aapt2 链接出基础 APK（二进制 Manifest + resources.arsc） ——
echo "[3/5] aapt2 链接基础 APK"
BASE="$TMP/base.apk"
"$AAPT2" link -o "$BASE" \
    --manifest "$MANIFEST" \
    -I "$ANDROID_JAR" \
    --min-sdk-version "$MIN_SDK" --target-sdk-version "$TARGET_SDK"

# —— 4) 塞入原生库，zipalign ——
echo "[4/5] 加入 jniLibs + zipalign"
( cd "$TMP" && zip -q -r "$BASE" lib )
ALIGNED="$TMP/aligned.apk"
"$ZIPALIGN" -f -p 4 "$BASE" "$ALIGNED"

# —— 5) 签名（调试 keystore，按需生成） ——
echo "[5/5] 签名"
KS="${ANDROID_KEYSTORE:-$ROOT/tools/debug.keystore}"
KS_PASS="${ANDROID_KEYSTORE_PASS:-android}"
KS_ALIAS="${ANDROID_KEY_ALIAS:-sincoding}"
if [[ ! -f "$KS" ]]; then
    echo "  生成调试 keystore: $KS"
    keytool -genkeypair -keystore "$KS" -storepass "$KS_PASS" -keypass "$KS_PASS" \
        -alias "$KS_ALIAS" -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Sincoding, OU=Game, O=Sincoding, L=NA, S=NA, C=NA" >/dev/null 2>&1
fi
mkdir -p "$(dirname "$OUT")"
"$APKSIGNER" sign --ks "$KS" --ks-pass "pass:$KS_PASS" --key-pass "pass:$KS_PASS" \
    --ks-key-alias "$KS_ALIAS" --out "$OUT" "$ALIGNED"
"$APKSIGNER" verify --print-certs "$OUT" >/dev/null && echo "签名校验通过 ✓"

echo "完成: $OUT"
ls -la "$OUT"
