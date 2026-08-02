cmake_minimum_required(VERSION 3.16)
project(sinc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(sinc
  src/main.cpp
  src/lexer.cpp
  src/parser.cpp
  src/type_checker.cpp
  src/codegen.cpp
  src/serializer.cpp
)
target_include_directories(sinc PRIVATE include)

if(MSVC)
  target_compile_options(sinc PRIVATE /W4 /utf-8)
else()
  target_compile_options(sinc PRIVATE -Wall -Wextra)
endif()
