// wasm_api.cpp — 把编译器暴露给浏览器（emscripten）
//
// 仅用于 wasm 构建（不参与原生 CMake 构建）。导出 sin_to_blocks：
// 源码 → 词法/语法/类型检查（尽力而为，容错）→ 积木模型 JSON + 诊断。
// 让编辑器用「规范引擎」做文本 → 积木 的反向同步。
#include "lexer.h"
#include "parser.h"
#include "serializer.h"
#include "type_checker.h"

#include <string>

using namespace sincoding;

static std::string jsonEscape(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;
            default: o += c;
        }
    }
    o += "\"";
    return o;
}

static std::string diagsJson(const std::vector<Diagnostic>& ds, bool& first) {
    std::string o;
    for (auto& d : ds) {
        if (!first) o += ",";
        first = false;
        o += "{\"line\":" + std::to_string(d.line) +
             ",\"col\":" + std::to_string(d.col) +
             ",\"msg\":" + jsonEscape(d.message) + "}";
    }
    return o;
}

extern "C" {

// 返回 {"blocks": <积木模型>, "diags": [{line,col,msg}...]} 的 JSON 字符串。
// 即使有语法/类型错误也尽量返回部分积木（编辑器据此显示"部分有效"状态）。
const char* sin_to_blocks(const char* src) {
    static std::string result; // 保持返回指针有效
    std::string source = src ? src : "";

    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    Program prog = parser.parseProgram();
    TypeChecker checker;
    checker.check(prog); // 忽略返回值，尽量填充类型

    bool first = true;
    std::string diags = "[";
    diags += diagsJson(lexer.errors(), first);
    diags += diagsJson(parser.errors(), first);
    diags += diagsJson(checker.errors(), first);
    diags += "]";

    result = "{\"blocks\":" + serializeBlocks(prog) + ",\"diags\":" + diags + "}";
    return result.c_str();
}

} // extern "C"
