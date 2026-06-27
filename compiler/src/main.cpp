// main.cpp — sinc 编译器命令行驱动
//
// 用法:
//   sinc <input.sin> [-o out.c]     将 Sincoding 源码转译为 C
//   sinc <input.sin> --tokens       仅打印 token（调试用）
#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "serializer.h"
#include "type_checker.h"

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
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) output = argv[++i];
        else if (a == "--emit" && i + 1 < argc) emit = argv[++i];
        else if (a == "--tokens") dumpTokens = true;
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
    if (!lexer.ok()) { printDiags(input, lexer.errors()); return 1; }

    // 2) 语法
    Parser parser(std::move(tokens));
    Program prog = parser.parseProgram();
    if (!parser.ok()) { printDiags(input, parser.errors()); return 1; }

    // 3) 类型检查
    TypeChecker checker;
    if (!checker.check(prog)) { printDiags(input, checker.errors()); return 1; }

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
        CodeGen gen;
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
