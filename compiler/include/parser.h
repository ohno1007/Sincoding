// parser.h — 手写递归下降 Parser
//
// 选择手写而非 yacc/bison：AST 要与积木双向同步，必须完全可控；
// 且编辑器需要错误恢复（语法错时仍尽量产出部分 AST）。
#pragma once
#include "ast.h"
#include "lexer.h"
#include "token.h"
#include <vector>

namespace sincoding {

class Parser {
public:
    Parser(std::vector<Token> tokens) : toks_(std::move(tokens)) {}

    Program parseProgram();

    const std::vector<Diagnostic>& errors() const { return errors_; }
    bool ok() const { return errors_.empty(); }

private:
    const Token& peek(int ahead = 0) const;
    const Token& cur() const { return peek(0); }
    bool check(TokKind k) const { return cur().kind == k; }
    bool match(TokKind k);
    const Token& advance();
    const Token& expect(TokKind k, const char* what);
    void error(const Token& at, const std::string& msg);
    void synchronize(); // 错误恢复：跳到下一个语句/声明边界

    // 文法规则
    FnPtr parseFn();
    StructPtr parseStruct();
    Type parseType(std::string& structName);
    BlockPtr parseBlock();
    StmtPtr parseStmt();
    StmtPtr parseLet();
    StmtPtr parseIf();
    StmtPtr parseWhile();
    StmtPtr parseFor();
    StmtPtr parseReturn();
    StmtPtr parseExprOrAssign();

    // 表达式（优先级爬升）
    ExprPtr parseExpr();
    ExprPtr parseOr();
    ExprPtr parseAnd();
    ExprPtr parseEquality();
    ExprPtr parseComparison();
    ExprPtr parseTerm();
    ExprPtr parseFactor();
    ExprPtr parseUnary();
    ExprPtr parsePrimary();

    std::vector<Token> toks_;
    int pos_ = 0;
    std::vector<Diagnostic> errors_;
    bool panic_ = false;
    bool noStructLit_ = false; // 在 if/while/for 条件中禁止 `Name { }` 结构体字面量（消歧义）
};

} // namespace sincoding
