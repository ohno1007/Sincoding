cmake_minimum_required(VERSION 3.16)
project(sinc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 标准库嵌入：构建前把 std/*.sin 生成为头文件（用户无需准备任何文件）
set(SIN_STD_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/std_modules.h)
# CONFIGURE_DEPENDS：新增 std/*.sin 时自动重新生成，无需手动 re-configure
file(GLOB SIN_STD_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../std/*.sin)
add_custom_command(
  OUTPUT ${SIN_STD_HEADER}
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_std_modules.sh ${SIN_STD_HEADER}
  DEPENDS ${SIN_STD_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_std_modules.sh
  COMMENT "生成内置标准库 std_modules.h"
  VERBATIM)
add_custom_target(sin_std_header DEPENDS ${SIN_STD_HEADER})

add_executable(sinc
  src/main.cpp
  src/lexer.cpp
  src/parser.cpp
  src/type_checker.cpp
  src/codegen.cpp
  src/serializer.cpp
  src/query.cpp
  src/modules.cpp
  src/generics.cpp
  src/blockreader.cpp
)
add_dependencies(sinc sin_std_header)
target_include_directories(sinc PRIVATE include ${CMAKE_CURRENT_BINARY_DIR}/generated)

if(MSVC)
  target_compile_options(sinc PRIVATE /W4 /utf-8)
else()
  target_compile_options(sinc PRIVATE -Wall -Wextra)
endif()
