// main.cpp — sinc 编译器命令行驱动
//
// 用法:
//   sinc <input.sin> [-o out.c]     将 Sincoding 源码转译为 C
//   sinc <input.sin> --tokens       仅打印 token（调试用）
#include "codegen.h"
#include "generics.h"
#include "lexer.h"
#include "modules.h"
#include "parser.h"
#include "query.h"
#include "serializer.h"
#include "type_checker.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace sincoding;

static std::string readFile(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { ok = false; return {}; }
    std::ostringstream ss;
    ss << f.rdbuf();
    ok = true;
    return ss.str();
}

static void printDiags(const std::string& file, const std::vector<Diagnostic>& diags) {
    for (auto& d : diags) {
        std::cerr << file << ":" << d.line;
        if (d.col > 0) std::cerr << ":" << d.col;
        std::cerr << ": 错误: " << d.message << "\n";
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "用法: sinc <input.sin> [-o out] [--emit c|src|blocks] [--tokens]\n"
                     "  --emit c      生成 C 代码（默认）\n"
                     "  --emit src    AST → 规范化 Sincoding 源码（文本视图）\n"
                     "  --emit blocks AST → 积木模型 JSON（积木视图）\n";
        return 2;
    }
    std::string input = argv[1];
    std::string output;
    std::string emit = "c";
    bool dumpTokens = false;
    bool debugBuild = false;   // --debug：注入调试钩子
    std::string queryKind;  // 非空表示 IDE 查询模式
    int qLine = 0, qCol = 0;
    std::string qNewName;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) output = argv[++i];
        else if (a == "--emit" && i + 1 < argc) emit = argv[++i];
        else if (a == "--tokens") dumpTokens = true;
        else if (a == "--debug") debugBuild = true;
        else if (a == "--query" && i + 3 < argc) {  // --query <hover|refs|complete|rename> <line> <col> [newName]
            queryKind = argv[++i];
            qLine = std::atoi(argv[++i]);
            qCol = std::atoi(argv[++i]);
            if (queryKind == "rename" && i + 1 < argc) qNewName = argv[++i];
        }
        else { std::cerr << "未知参数: " << a << "\n"; return 2; }
    }
    if (emit != "c" && emit != "src" && emit != "blocks") {
        std::cerr << "未知 --emit 取值: " << emit << "（应为 c|src|blocks）\n";
        return 2;
    }

    bool ok = false;
    std::string src = readFile(input, ok);
    if (!ok) { std::cerr << "无法打开文件: " << input << "\n"; return 2; }

    // 1) 词法
    Lexer lexer(src);
    auto tokens = lexer.tokenize();
    if (dumpTokens) {
        for (auto& t : tokens)
            std::cout << t.line << ":" << t.col << "\t" << tokKindName(t.kind)
                      << "\t'" << t.text << "'\n";
    }
    // IDE 查询（尤其补全）总是在「写到一半」的源码上发生，语法错误不能中止查询，
    // 否则命令行与 wasm（一向容错）行为不一致。
    bool tolerant = !queryKind.empty();
    if (!lexer.ok() && !tolerant) { printDiags(input, lexer.errors()); return 1; }

    // 2) 语法
    Parser parser(std::move(tokens));
    Program prog = parser.parseProgram();
    if (!parser.ok() && !tolerant) { printDiags(input, parser.errors()); return 1; }
    attachComments(prog, lexer.comments());   // 注释挂回 AST（--emit src / rename 原样写回）

    // 2.5) 解析 import：把模块声明合并进来（内置标准库 + 同目录 + SINCODING_PATH）
    {
        auto slash = input.find_last_of('/');
        std::string baseDir = (slash == std::string::npos) ? "." : input.substr(0, slash);
        std::vector<Diagnostic> modDiags;
        if (!resolveImports(prog, baseDir, modDiags) && !tolerant) { printDiags(input, modDiags); return 1; }
    }

    // 3) 类型检查（含泛型单态化：把泛型调用实例化成具体函数）
    std::vector<Diagnostic> checkDiags;
    bool checkOk = checkWithGenerics(prog, checkDiags);

    // IDE 查询模式：尽力而为（即使有类型错误也返回可用结果）
    if (!queryKind.empty()) {
        if (queryKind == "hover") std::cout << queryHover(prog, qLine, qCol) << "\n";
        else if (queryKind == "refs") std::cout << queryReferences(prog, qLine, qCol) << "\n";
        else if (queryKind == "complete") std::cout << queryComplete(prog, src, qLine, qCol) << "\n";
        else if (queryKind == "rename") std::cout << applyRename(prog, qLine, qCol, qNewName) << "\n";
        else { std::cerr << "未知查询类型: " << queryKind << "\n"; return 2; }
        return 0;
    }

    if (!checkOk) { printDiags(input, checkDiags); return 1; }

    // 4) 按 --emit 输出
    std::string result;
    const char* label;
    if (emit == "src") {
        result = serializeSource(prog);
        label = "已生成源码";
    } else if (emit == "blocks") {
        result = serializeBlocks(prog);
        label = "已生成积木模型";
    } else {
        CodeGen gen(debugBuild);
        result = gen.generate(prog);
        label = "已生成 C 代码";
    }

    if (output.empty()) {
        std::cout << result;
    } else {
        std::ofstream out(output, std::ios::binary);
        if (!out) { std::cerr << "无法写出文件: " << output << "\n"; return 2; }
        out << result;
        std::cerr << label << ": " << output << "\n";
    }
    return 0;
}
