# templates/web/CMakeLists.txt — Sincoding 程序 → Web(wasm) 成品
#
# 通过 emscripten 工具链构建（emcmake cmake ...）。由 tools/build_web.sh 调用，
# 注入以下变量：
#   SIN_PROGRAM_C       由 sinc 生成的 C 源文件
#   SIN_RUNTIME_DIR     runtime/ 目录（prelude.c / runtime.c / 头文件）
#   RAYLIB_WEB_LIB      raylib 的 web 静态库（emcc 构建的 libraylib.a）
#   RAYLIB_WEB_INCLUDE  raylib 头文件目录
cmake_minimum_required(VERSION 3.16)
project(sincoding_web C)
set(CMAKE_C_STANDARD 11)

if(NOT DEFINED SIN_PROGRAM_C)
  message(FATAL_ERROR "请用 -DSIN_PROGRAM_C=<生成的 C> 指定程序源码")
endif()

add_executable(index
  ${SIN_PROGRAM_C}
  ${SIN_RUNTIME_DIR}/prelude.c
  ${SIN_RUNTIME_DIR}/runtime.c
  ${CMAKE_CURRENT_SOURCE_DIR}/emscripten_compat.c
)
target_include_directories(index PRIVATE ${SIN_RUNTIME_DIR} ${RAYLIB_WEB_INCLUDE})
target_link_libraries(index ${RAYLIB_WEB_LIB})

# 产出 .html（含 .js + .wasm）；ASYNCIFY 让 while 主循环在浏览器中可用
set_target_properties(index PROPERTIES SUFFIX ".html")
target_link_options(index PRIVATE
  "-sUSE_GLFW=3"
  "-sASYNCIFY"
  "-sALLOW_MEMORY_GROWTH=1"
  "-sFORCE_FILESYSTEM=1"
  "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
)

# 造型资源：把 SIN_ASSETS_DIR 里的 PNG 预载进虚拟文件系统根，
# 这样 sprite_load("coin.png") 在浏览器里也能加载——与桌面/安卓同源。
if(DEFINED SIN_ASSETS_DIR AND EXISTS "${SIN_ASSETS_DIR}")
  target_link_options(index PRIVATE "--preload-file" "${SIN_ASSETS_DIR}@/")
endif()
