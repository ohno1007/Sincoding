// modules.h — import 模块解析：把被导入模块的声明合并进主程序
//
// 解析顺序：
//   1) **内置标准库**（std/*.sin 在构建期嵌入编译器，见 tools/gen_std_modules.sh）
//      —— 因此 import "std/mathx" 在原生 CLI 与浏览器 wasm 里都直接可用
//   2) 相对导入方的目录：<baseDir>/<name>.sin
//   3) 环境变量 SINCODING_PATH 中的目录（冒号分隔）
//
// 合并后的声明会带上 module 标记：类型检查/代码生成看得见它们，
// 而 --emit src / --emit blocks 会跳过它们（只写回 import 行），保证往返幂等。
#pragma once
#include "ast.h"
#include "lexer.h"   // Diagnostic
#include <string>
#include <vector>

namespace sincoding {

// 递归解析 prog.imports 并把模块声明合并进 prog。
// baseDir 为导入方源文件所在目录（浏览器内传空字符串，只用内置库）。
// 返回是否全部解析成功；失败信息追加到 diags。
bool resolveImports(Program& prog, const std::string& baseDir, std::vector<Diagnostic>& diags);

} // namespace sincoding
