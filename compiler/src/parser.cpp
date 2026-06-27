#include "parser.h"
#include <cstdlib>

namespace sincoding {

const Token& Parser::peek(int ahead) const {
    int p = pos_ + ahead;
    if (p >= (int)toks_.size()) return toks_.back(); // End
    return toks_[p];
}

const Token& Parser::advance() {
    if (pos_ < (int)toks_.size() - 1) pos_++;
    return toks_[pos_ - 1];
}

bool Parser::match(TokKind k) {
    if (check(k)) { advance(); return true; }
    return false;
}

void Parser::error(const Token& at, const std::string& msg) {
    if (panic_) return; // 抑制级联错误
    panic_ = true;
    errors_.push_back({at.line, at.col, msg});
}

const Token& Parser::expect(TokKind k, const char* what) {
    if (check(k)) { panic_ = false; return advance(); }
    error(cur(), std::string("期望 ") + what + "，但遇到 '" +
                     (cur().text.empty() ? tokKindName(cur().kind) : cur().text) + "'");
    return cur();
}

void Parser::synchronize() {
    panic_ = false;
    while (!check(TokKind::End)) {
        if (toks_[pos_ - 1].kind == TokKind::Semicolon) return;
        switch (cur().kind) {
            case TokKind::KwFn:
            case TokKind::KwLet:
            case TokKind::KwIf:
            case TokKind::KwWhile:
            case TokKind::KwReturn:
            case TokKind::RBrace:
                return;
            default:
                advance();
        }
    }
}

Type Parser::parseType() {
    switch (cur().kind) {
        case TokKind::KwTypeInt: advance(); return Type::Int;
        case TokKind::KwTypeFloat: advance(); return Type::Float;
        case TokKind::KwTypeBool: advance(); return Type::Bool;
        case TokKind::KwTypeVoid: advance(); return Type::Void;
        default:
            error(cur(), "期望类型名 (int/float/bool/void)");
            return Type::Unknown;
    }
}

Program Parser::parseProgram() {
    Program prog;
    while (!check(TokKind::End)) {
        if (check(TokKind::KwFn) || check(TokKind::KwExtern)) {
            FnPtr fn = parseFn();
            if (fn) prog.fns.push_back(std::move(fn));
            if (panic_) synchronize();
        } else {
            error(cur(), "顶层只允许函数声明 (fn ... 或 extern fn ...)");
            synchronize();
        }
    }
    return prog;
}

FnPtr Parser::parseFn() {
    auto fn = std::make_unique<FnDecl>();
    fn->line = cur().line;
    fn->isExtern = match(TokKind::KwExtern); // 'extern' 前缀可选
    expect(TokKind::KwFn, "'fn'");
    const Token& name = expect(TokKind::Ident, "函数名");
    fn->name = name.text;
    expect(TokKind::LParen, "'('");
    if (!check(TokKind::RParen)) {
        do {
            const Token& p = expect(TokKind::Ident, "参数名");
            Param param;
            param.name = p.text;
            param.line = p.line;
            expect(TokKind::Colon, "':'");
            param.type = parseType();
            fn->params.push_back(param);
        } while (match(TokKind::Comma));
    }
    expect(TokKind::RParen, "')'");
    if (match(TokKind::Arrow)) fn->ret = parseType();
    else fn->ret = Type::Void;
    if (fn->isExtern) {
        // extern 声明没有函数体，分号可选
        match(TokKind::Semicolon);
    } else {
        fn->body = parseBlock();
    }
    return fn;
}

BlockPtr Parser::parseBlock() {
    auto block = std::make_unique<Block>();
    block->line = cur().line;
    expect(TokKind::LBrace, "'{'");
    while (!check(TokKind::RBrace) && !check(TokKind::End)) {
        StmtPtr s = parseStmt();
        if (s) block->stmts.push_back(std::move(s));
        if (panic_) synchronize();
    }
    expect(TokKind::RBrace, "'}'");
    return block;
}

StmtPtr Parser::parseStmt() {
    switch (cur().kind) {
        case TokKind::KwLet: return parseLet();
        case TokKind::KwIf: return parseIf();
        case TokKind::KwWhile: return parseWhile();
        case TokKind::KwReturn: return parseReturn();
        default: return parseExprOrAssign();
    }
}

StmtPtr Parser::parseLet() {
    auto s = std::make_unique<LetStmt>();
    s->line = cur().line;
    expect(TokKind::KwLet, "'let'");
    s->name = expect(TokKind::Ident, "变量名").text;
    if (match(TokKind::Colon)) s->declared = parseType();
    expect(TokKind::Assign, "'='");
    s->init = parseExpr();
    match(TokKind::Semicolon); // 分号可选
    return s;
}

StmtPtr Parser::parseIf() {
    auto s = std::make_unique<IfStmt>();
    s->line = cur().line;
    expect(TokKind::KwIf, "'if'");
    s->cond = parseExpr();
    s->thenBlock = parseBlock();
    if (match(TokKind::KwElse)) {
        if (check(TokKind::KwIf)) {
            // else if：包装成一个只含 IfStmt 的 block
            auto wrap = std::make_unique<Block>();
            wrap->line = cur().line;
            wrap->stmts.push_back(parseIf());
            s->elseBlock = std::move(wrap);
        } else {
            s->elseBlock = parseBlock();
        }
    }
    return s;
}

StmtPtr Parser::parseWhile() {
    auto s = std::make_unique<WhileStmt>();
    s->line = cur().line;
    expect(TokKind::KwWhile, "'while'");
    s->cond = parseExpr();
    s->body = parseBlock();
    return s;
}

StmtPtr Parser::parseReturn() {
    auto s = std::make_unique<ReturnStmt>();
    s->line = cur().line;
    expect(TokKind::KwReturn, "'return'");
    if (!check(TokKind::Semicolon) && !check(TokKind::RBrace))
        s->value = parseExpr();
    match(TokKind::Semicolon);
    return s;
}

StmtPtr Parser::parseExprOrAssign() {
    int line = cur().line;
    // 赋值：Ident '=' expr
    if (check(TokKind::Ident) && peek(1).kind == TokKind::Assign) {
        auto s = std::make_unique<AssignStmt>();
        s->line = line;
        s->name = advance().text; // ident
        advance();                // '='
        s->value = parseExpr();
        match(TokKind::Semicolon);
        return s;
    }
    auto s = std::make_unique<ExprStmt>();
    s->line = line;
    s->expr = parseExpr();
    match(TokKind::Semicolon);
    return s;
}

// ---------- 表达式：优先级爬升 ----------

ExprPtr Parser::parseExpr() { return parseOr(); }

static ExprPtr makeBinary(std::string op, ExprPtr l, ExprPtr r, int line) {
    auto b = std::make_unique<Binary>();
    b->op = std::move(op);
    b->lhs = std::move(l);
    b->rhs = std::move(r);
    b->line = line;
    return b;
}

ExprPtr Parser::parseOr() {
    ExprPtr e = parseAnd();
    while (check(TokKind::OrOr)) {
        int line = cur().line; advance();
        e = makeBinary("||", std::move(e), parseAnd(), line);
    }
    return e;
}

ExprPtr Parser::parseAnd() {
    ExprPtr e = parseEquality();
    while (check(TokKind::AndAnd)) {
        int line = cur().line; advance();
        e = makeBinary("&&", std::move(e), parseEquality(), line);
    }
    return e;
}

ExprPtr Parser::parseEquality() {
    ExprPtr e = parseComparison();
    while (check(TokKind::Eq) || check(TokKind::Ne)) {
        std::string op = check(TokKind::Eq) ? "==" : "!=";
        int line = cur().line; advance();
        e = makeBinary(op, std::move(e), parseComparison(), line);
    }
    return e;
}

ExprPtr Parser::parseComparison() {
    ExprPtr e = parseTerm();
    while (check(TokKind::Lt) || check(TokKind::Le) ||
           check(TokKind::Gt) || check(TokKind::Ge)) {
        std::string op = cur().text;
        int line = cur().line; advance();
        e = makeBinary(op, std::move(e), parseTerm(), line);
    }
    return e;
}

ExprPtr Parser::parseTerm() {
    ExprPtr e = parseFactor();
    while (check(TokKind::Plus) || check(TokKind::Minus)) {
        std::string op = check(TokKind::Plus) ? "+" : "-";
        int line = cur().line; advance();
        e = makeBinary(op, std::move(e), parseFactor(), line);
    }
    return e;
}

ExprPtr Parser::parseFactor() {
    ExprPtr e = parseUnary();
    while (check(TokKind::Star) || check(TokKind::Slash) || check(TokKind::Percent)) {
        std::string op = cur().text;
        int line = cur().line; advance();
        e = makeBinary(op, std::move(e), parseUnary(), line);
    }
    return e;
}

ExprPtr Parser::parseUnary() {
    if (check(TokKind::Minus) || check(TokKind::Not)) {
        auto u = std::make_unique<Unary>();
        u->op = check(TokKind::Minus) ? "-" : "!";
        u->line = cur().line;
        advance();
        u->operand = parseUnary();
        return u;
    }
    return parsePrimary();
}

ExprPtr Parser::parsePrimary() {
    const Token& t = cur();
    switch (t.kind) {
        case TokKind::Int: {
            auto e = std::make_unique<IntLit>();
            e->line = t.line;
            e->value = std::strtoll(t.text.c_str(), nullptr, 10);
            advance();
            return e;
        }
        case TokKind::Float: {
            auto e = std::make_unique<FloatLit>();
            e->line = t.line;
            e->value = std::strtod(t.text.c_str(), nullptr);
            advance();
            return e;
        }
        case TokKind::KwTrue:
        case TokKind::KwFalse: {
            auto e = std::make_unique<BoolLit>();
            e->line = t.line;
            e->value = (t.kind == TokKind::KwTrue);
            advance();
            return e;
        }
        case TokKind::Ident: {
            std::string name = t.text;
            int line = t.line;
            advance();
            if (check(TokKind::LParen)) {
                auto call = std::make_unique<Call>();
                call->callee = name;
                call->line = line;
                advance(); // '('
                if (!check(TokKind::RParen)) {
                    do { call->args.push_back(parseExpr()); } while (match(TokKind::Comma));
                }
                expect(TokKind::RParen, "')'");
                return call;
            }
            auto v = std::make_unique<Var>();
            v->name = name;
            v->line = line;
            return v;
        }
        case TokKind::LParen: {
            advance();
            ExprPtr e = parseExpr();
            expect(TokKind::RParen, "')'");
            return e;
        }
        default:
            error(t, std::string("期望表达式，但遇到 '") +
                         (t.text.empty() ? tokKindName(t.kind) : t.text) + "'");
            // 返回一个占位整型，保证 AST 不为空指针
            auto e = std::make_unique<IntLit>();
            e->line = t.line;
            e->value = 0;
            if (!check(TokKind::End)) advance();
            return e;
    }
}

} // namespace sincoding
