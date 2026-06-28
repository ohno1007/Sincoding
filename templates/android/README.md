# Android 构建模板

把 Sincoding 程序打包成 `.apk`。分两步：

## 1. 交叉编译原生库（NDK）

由 `tools/build_android.sh` 用 Android NDK 把
`sinc 生成的 C + runtime + raylib(Android)` 编成 `libsincoding.so`：

```bash
# 需要 Android NDK（ANDROID_NDK 指向其路径）+ raylib 的 Android 静态库
ANDROID_NDK=/usr/lib/android-ndk \
  tools/build_android.sh examples/game.sin templates/android/jniLibs/arm64-v8a/libsincoding.so
```

入口为 raylib NativeActivity 提供的 `ANativeActivity_onCreate`（来自 raylib Android 库），
它会调用程序的 `main()`。

## 2. 套 APK 壳（Gradle，需 Android SDK）

本目录是一个 app 模块骨架：

- `AndroidManifest.xml` —— NativeActivity，`android.app.lib_name = sincoding`
- `build.gradle` —— 把 `jniLibs/<abi>/libsincoding.so` 打进 APK
- `jniLibs/arm64-v8a/libsincoding.so` —— 上一步产物

用 Android Studio 打开，或命令行（需 SDK + Gradle）：

```bash
gradle assembleRelease        # 产出 app-release-unsigned.apk
# 再用 apksigner 签名即可安装
```

> 多架构：在 `build.gradle` 的 `abiFilters` 增加 `armeabi-v7a`、`x86_64` 等，
> 并为每个 ABI 各编译一份 `.so` 放入对应 `jniLibs/<abi>/`。
