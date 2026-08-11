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

// 光标 (line,col) 处标识符的所有引用位置 JSON：
//   {"found":true,"name":"x","refs":[{"line":2,"col":9,"len":1},...]} 或 {"found":false}
std::string queryReferences(const Program& prog, int line, int col);

// 把光标处标识符改名为 newName，返回 {"ok":true,"source":"<新源码>","note":"..."}。
// 会修改传入的 prog（调用方每次查询都重新 parse，故无副作用顾虑）。
std::string applyRename(Program& prog, int line, int col, const std::string& newName);

// 代码补全：按光标处的语境（成员 / 类型位置 / 普通标识符）给候选，
// 候选带真实类型（作用域内的变量、含导入库在内的全部函数、结构体字段）：
//   {"ctx":"member","prefix":"x","items":[{"text":"x","kind":"field","detail":"float"}]}
// 需要源文本判断语境（`.` / `:` / `->`），AST 提供类型与作用域。
std::string queryComplete(const Program& prog, const std::string& src, int line, int col);

} // namespace sincoding
