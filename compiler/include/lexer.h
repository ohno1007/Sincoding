// lexer.h — 手写词法分析器
#pragma once
#include "token.h"
#include <string>
#include <vector>

namespace sincoding {

struct Diagnostic {
    int line;
    int col;
    std::string message;
};

// 行注释（token 流之外单独收集，供 attachComments 按行挂回 AST——
// 注释是用户的话，序列化时必须原样写回，不许无声吃掉）
struct CommentTok {
    int line;
    int col;           // "//" 的起始列
    std::string text;  // "//" 之后的原文（含前导空格），写回时输出 "//" + text
};

class Lexer {
public:
    explicit Lexer(std::string src) : src_(std::move(src)) {}

    // 一次性扫描出全部 token（末尾含一个 End）。
    std::vector<Token> tokenize();

    const std::vector<Diagnostic>& errors() const { return errors_; }
    const std::vector<CommentTok>& comments() const { return comments_; }
    bool ok() const { return errors_.empty(); }

private:
    char peek(int ahead = 0) const;
    char advance();
    bool match(char expected);
    bool atEnd() const { return pos_ >= (int)src_.size(); }

    void addToken(TokKind kind, std::string text);
    void error(const std::string& msg);
    void lexNumber();
    void lexIdentOrKeyword();
    void lexString();

    std::string src_;
    int pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    int tokStartCol_ = 1;
    std::vector<Token> tokens_;
    std::vector<Diagnostic> errors_;
    std::vector<CommentTok> comments_;
};

} // namespace sincoding
