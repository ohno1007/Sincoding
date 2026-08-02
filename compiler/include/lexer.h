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

class Lexer {
public:
    explicit Lexer(std::string src) : src_(std::move(src)) {}

    // 一次性扫描出全部 token（末尾含一个 End）。
    std::vector<Token> tokenize();

    const std::vector<Diagnostic>& errors() const { return errors_; }
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
};

} // namespace sincoding
