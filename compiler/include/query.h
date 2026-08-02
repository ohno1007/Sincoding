// query.h — IDE 查询（mini-LSP）：悬停类型 / 查找引用 / 重命名
//
// 基于 AST（唯一真相）+ 标识符位置（Expr.col 等）。原生 sinc 与 wasm 共用，
// 便于 bash 端到端测试与浏览器编辑器接入。
#pragma once
#include "ast.h"
#include <string>

namespace sincoding {

// 返回光标 (line,col) 处标识符的类型信息 JSON：
//   {"found":true,"name":"x","kind":"变量","type":"int[5]"} 或 {"found":false}
std::string queryHover(const Program& prog, int line, int col);

} // namespace sincoding
