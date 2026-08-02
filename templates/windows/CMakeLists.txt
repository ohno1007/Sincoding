# templates/windows/CMakeLists.txt — Sincoding 程序 → Windows .exe
#
# 通过 MinGW-w64 工具链交叉编译（见 mingw-toolchain.cmake）。由
# tools/build_windows.sh 调用，注入：
#   SIN_PROGRAM_C       由 sinc 生成的 C 源文件
#   SIN_RUNTIME_DIR     runtime/ 目录
#   RAYLIB_WIN_LIB      raylib 的 Windows 静态库（MinGW 构建）
#   RAYLIB_WIN_INCLUDE  raylib 头文件目录
cmake_minimum_required(VERSION 3.16)
project(sincoding_win C)
set(CMAKE_C_STANDARD 11)

if(NOT DEFINED SIN_PROGRAM_C)
  message(FATAL_ERROR "请用 -DSIN_PROGRAM_C=<生成的 C> 指定程序源码")
endif()

add_executable(game
  ${SIN_PROGRAM_C}
  ${SIN_RUNTIME_DIR}/prelude.c
  ${SIN_RUNTIME_DIR}/runtime.c
)
target_include_directories(game PRIVATE ${SIN_RUNTIME_DIR} ${RAYLIB_WIN_INCLUDE})
# raylib(Windows) 依赖的系统库
target_link_libraries(game ${RAYLIB_WIN_LIB} opengl32 gdi32 winmm)
# 静态链接 MinGW 运行时，产出单文件免依赖 exe
target_link_options(game PRIVATE -static -static-libgcc)
set_target_properties(game PROPERTIES OUTPUT_NAME game SUFFIX ".exe")
