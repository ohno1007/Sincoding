// wasm_api.cpp — 把编译器暴露给浏览器（emscripten）
//
// 仅用于 wasm 构建（不参与原生 CMake 构建）。导出 sin_to_blocks：
// 源码 → 词法/语法/类型检查（尽力而为，容错）→ 积木模型 JSON + 诊断。
// 让编辑器用「规范引擎」做文本 → 积木 的反向同步。
#include "blockreader.h"
#include "generics.h"
#include "lexer.h"
#include "modules.h"
#include "parser.h"
#include "query.h"
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
    std::vector<Diagnostic> modDiags;
    resolveImports(prog, "", modDiags);   // 浏览器内：只用内置标准库
    injectBuiltinModule(prog, "std/stage");   // 运行时 API 隐式可见（见 std/stage.sin）
    std::vector<Diagnostic> checkDiags;
    checkWithGenerics(prog, checkDiags);   // 尽量填充类型 + 单态化泛型

    bool first = true;
    std::string diags = "[";
    diags += diagsJson(lexer.errors(), first);
    diags += diagsJson(parser.errors(), first);
    diags += diagsJson(modDiags, first);
    diags += diagsJson(checkDiags, first);
    diags += "]";

    result = "{\"blocks\":" + serializeBlocks(prog) + ",\"diags\":" + diags + "}";
    return result.c_str();
}

// IDE 悬停：返回 (line,col) 处标识符的类型信息 JSON（尽力而为，容错）。
const char* sin_hover(const char* src, int line, int col) {
    static std::string result;
    std::string source = src ? src : "";
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    std::vector<Diagnostic> modDiags;
    resolveImports(prog, "", modDiags);   // 浏览器内：只用内置标准库
    injectBuiltinModule(prog, "std/stage");   // 运行时 API 隐式可见（见 std/stage.sin）
    std::vector<Diagnostic> cd; checkWithGenerics(prog, cd);
    result = queryHover(prog, line, col);
    return result.c_str();
}

// IDE 查找引用：返回 (line,col) 处标识符的所有引用位置 JSON。
const char* sin_references(const char* src, int line, int col) {
    static std::string result;
    std::string source = src ? src : "";
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    std::vector<Diagnostic> modDiags;
    resolveImports(prog, "", modDiags);   // 浏览器内：只用内置标准库
    injectBuiltinModule(prog, "std/stage");   // 运行时 API 隐式可见（见 std/stage.sin）
    std::vector<Diagnostic> cd; checkWithGenerics(prog, cd);
    result = queryReferences(prog, line, col);
    return result.c_str();
}

// IDE 代码补全：返回 (line,col) 处的候选 JSON（语境 + 真实类型，含导入库符号）。
const char* sin_complete(const char* src, int line, int col) {
    static std::string result;
    std::string source = src ? src : "";
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    std::vector<Diagnostic> modDiags;
    resolveImports(prog, "", modDiags);   // 浏览器内：只用内置标准库
    injectBuiltinModule(prog, "std/stage");   // 运行时 API 隐式可见（见 std/stage.sin）
    std::vector<Diagnostic> cd; checkWithGenerics(prog, cd);
    result = queryComplete(prog, source, line, col);
    return result.c_str();
}

// IDE 重命名：把 (line,col) 处标识符改名为 newName，返回 {ok,source,note}。
const char* sin_rename(const char* src, int line, int col, const char* newName) {
    static std::string result;
    std::string source = src ? src : "";
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    std::vector<Diagnostic> modDiags;
    resolveImports(prog, "", modDiags);   // 浏览器内：只用内置标准库
    injectBuiltinModule(prog, "std/stage");   // 运行时 API 隐式可见（见 std/stage.sin）
    std::vector<Diagnostic> cd; checkWithGenerics(prog, cd);
    result = applyRename(prog, line, col, newName ? newName : "");
    return result.c_str();
}

// 运行时 API 的语言侧声明源码（std/stage.sin 原文）。
// 编辑器导出 .sin 时把它补在文件头部，使程序可独立编译——
// 声明只此一份，前端不再另抄一张表。
const char* sin_runtime_decls(void) {
    static std::string result;
    if (result.empty() && !builtinModuleSource("std/stage", result)) result = "";
    return result.c_str();
}

// 积木 → 文本：编辑器改完积木后由**引擎**写回源码（唯一序列化器，替代 JS 镜像）。
// 返回 {"ok":true,"source":"..."} 或 {"ok":false,"err":"..."}。
const char* sin_blocks_to_src(const char* blocksJson) {
    static std::string result;
    bool ok = false; std::string err;
    Program prog = blocksToProgram(blocksJson ? blocksJson : "", ok, err);
    if (!ok) { result = "{\"ok\":false,\"err\":" + jsonEscape(err) + "}"; return result.c_str(); }
    result = "{\"ok\":true,\"source\":" + jsonEscape(serializeSource(prog)) + "}";
    return result.c_str();
}

} // extern "C"
