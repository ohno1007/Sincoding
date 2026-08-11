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

// 把某个内置模块的声明**隐式**并入 prog（不写进 prog.imports，因此不影响序列化往返）。
// 图形化编辑器用它加载 std/stage（运行时 API）：积木调用 sprite_new 之类不必先写
// extern 声明，也就不会被报成「未定义的函数」，补全同样能看到真实签名。
// 与主程序重名的 extern 原型自动跳过。返回是否找到该模块。
bool injectBuiltinModule(Program& prog, const std::string& name);

// 取内置模块的源码（编辑器导出 .sin 时用它补运行时声明，避免前端另抄一份）。
// 找不到返回 false。
bool builtinModuleSource(const std::string& name, std::string& out);

} // namespace sincoding
