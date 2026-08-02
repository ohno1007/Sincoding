// serializer.h — AST 的两种序列化
//
// 体现「AST 是唯一真相源」：
//   - serializeSource: AST → Sincoding 源码文本（文本编辑视图）
//   - serializeBlocks: AST → 积木模型 JSON（积木可视化视图）
// 积木与文本都由 AST 派生，二者绝不直接互转。
#pragma once
#include "ast.h"
#include <string>

namespace sincoding {

// AST → 规范化的 Sincoding 源码（可被重新 parse，幂等）
std::string serializeSource(const Program& prog);

// AST → 积木模型 JSON（供编辑器/查看器渲染为嵌套积木）
std::string serializeBlocks(const Program& prog);

} // namespace sincoding
