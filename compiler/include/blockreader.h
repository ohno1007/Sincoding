// blockreader.h — 积木模型 JSON → AST（serializeBlocks 的逆向）
//
// 用途：编辑器改完积木后要写回文本。此前这一步由 editor/blockmodel.js **镜像**
// C++ 序列化器实现（双份实现），语言每加一个特性就要写两遍——已经漂移出真实 bug
// （import 行被丢、泛型 <T> 与数组长度 T[N] 丢失）。
//
// 现在改为：积木 JSON 交回引擎，引擎读成 AST 再用**唯一的** serializeSource 输出。
// 于是「引擎单例」这条架构不变量成立，序列化只剩 C++ 一份实现。
#pragma once
#include "ast.h"
#include <string>

namespace sincoding {

// 解析 serializeBlocks 产出的 JSON（含 imports/structs/globals/program）为 Program。
// 解析失败时 ok=false，err 给出原因。
Program blocksToProgram(const std::string& json, bool& ok, std::string& err);

} // namespace sincoding
