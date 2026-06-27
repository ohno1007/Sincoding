// emscripten_compat.c — 补齐旧版 emscripten(3.1.6) GLFW 模拟缺失的符号
//
// raylib 5.5 的 rcore 会调用部分较新的 GLFW 回调设置函数，而 apt 版
// emscripten 自带的 GLFW 模拟尚未提供。这些回调对本运行时并非必需，
// 提供空实现以满足链接即可（签名用 void* 以匹配 ABI，C 链接不校验类型）。
void* glfwSetWindowContentScaleCallback(void* window, void* cbfun) {
    (void)window; (void)cbfun; return 0;
}
