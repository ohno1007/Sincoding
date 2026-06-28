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

Type Parser::parseType(std::string& structName) {
    switch (cur().kind) {
        case TokKind::KwTypeInt: advance(); return Type::Int;
        case TokKind::KwTypeFloat: advance(); return Type::Float;
        case TokKind::KwTypeBool: advance(); return Type::Bool;
        case TokKind::KwTypeVoid: advance(); return Type::Void;
        case TokKind::KwTypeString: advance(); return Type::String;
        case TokKind::Ident: structName = advance().text; return Type::Struct; // 结构体名
        default:
            error(cur(), "期望类型名 (int/float/bool/string/void/结构体名)");
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
        } else if (check(TokKind::KwStruct)) {
            StructPtr st = parseStruct();
            if (st) prog.structs.push_back(std::move(st));
            if (panic_) synchronize();
        } else if (check(TokKind::KwLet)) {
            StmtPtr g = parseLet();   // 顶层全局变量
            if (g) prog.globals.push_back(std::move(g));
            if (panic_) synchronize();
        } else {
            error(cur(), "顶层只允许结构体 (struct ...)、全局变量 (let ...) 或函数 (fn ... / extern fn ...)");
            synchronize();
        }
    }
    return prog;
}

StructPtr Parser::parseStruct() {
    auto s = std::make_unique<StructDecl>();
    s->line = cur().line;
    expect(TokKind::KwStruct, "'struct'");
    s->name = expect(TokKind::Ident, "结构体名").text;
    expect(TokKind::LBrace, "'{'");
    while (!check(TokKind::RBrace) && !check(TokKind::End)) {
        StructField f;
        f.line = cur().line;
        f.name = expect(TokKind::Ident, "字段名").text;
        expect(TokKind::Colon, "':'");
        std::string sn;
        f.type = parseType(sn);   // 字段类型（标量；结构体名由类型检查拒绝）
        s->fields.push_back(f);
        match(TokKind::Comma);    // 逗号可选（换行亦可分隔）
    }
    expect(TokKind::RBrace, "'}'");
    return s;
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
            param.type = parseType(param.structName);
            fn->params.push_back(param);
        } while (match(TokKind::Comma));
    }
    expect(TokKind::RParen, "')'");
    if (match(TokKind::Arrow)) fn->ret = parseType(fn->retStruct);
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
        case TokKind::KwFor: return parseFor();
        case TokKind::KwReturn: return parseReturn();
        default: return parseExprOrAssign();
    }
}

StmtPtr Parser::parseLet() {
    auto s = std::make_unique<LetStmt>();
    s->line = cur().line;
    expect(TokKind::KwLet, "'let'");
    s->name = expect(TokKind::Ident, "变量名").text;
    if (match(TokKind::Colon)) {
        s->declared = parseType(s->structName);
        if (match(TokKind::LBracket)) { // 数组类型 T[N]
            const Token& n = expect(TokKind::Int, "数组长度");
            s->declaredLen = (int)std::strtoll(n.text.c_str(), nullptr, 10);
            expect(TokKind::RBracket, "']'");
        }
    }
    // 初始化可选：有类型标注时可省略（零初始化）
    if (match(TokKind::Assign)) s->init = parseExpr();
    match(TokKind::Semicolon); // 分号可选
    return s;
}

StmtPtr Parser::parseIf() {
    auto s = std::make_unique<IfStmt>();
    s->line = cur().line;
    expect(TokKind::KwIf, "'if'");
    noStructLit_ = true;
    s->cond = parseExpr();
    noStructLit_ = false;
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
    noStructLit_ = true;
    s->cond = parseExpr();
    noStructLit_ = false;
    s->body = parseBlock();
    return s;
}

StmtPtr Parser::parseFor() {
    auto s = std::make_unique<ForStmt>();
    s->line = cur().line;
    expect(TokKind::KwFor, "'for'");
    s->var = expect(TokKind::Ident, "循环变量名").text;
    // 'in' 不是关键字，用标识符 "in" 表示
    if (check(TokKind::Ident) && cur().text == "in") advance();
    else error(cur(), "for 循环需要 'in'（形如 for i in 0..n）");
    noStructLit_ = true;
    s->start = parseExpr();
    expect(TokKind::DotDot, "'..'");
    s->end = parseExpr();
    noStructLit_ = false;
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
    // 字段赋值：Ident '.' Ident '=' expr
    if (check(TokKind::Ident) && peek(1).kind == TokKind::Dot &&
        peek(2).kind == TokKind::Ident && peek(3).kind == TokKind::Assign) {
        auto s = std::make_unique<AssignStmt>();
        s->line = line;
        s->name = advance().text; // ident
        advance();                // '.'
        s->field = advance().text;
        advance();                // '='
        s->value = parseExpr();
        match(TokKind::Semicolon);
        return s;
    }
    // 元素赋值或下标表达式：Ident '[' expr ']' ...
    if (check(TokKind::Ident) && peek(1).kind == TokKind::LBracket) {
        std::string name = advance().text; // ident
        advance();                          // '['
        ExprPtr idx = parseExpr();
        expect(TokKind::RBracket, "']'");
        if (check(TokKind::Assign)) {        // name[idx] = value
            advance();
            auto s = std::make_unique<AssignStmt>();
            s->line = line; s->name = name; s->index = std::move(idx);
            s->value = parseExpr();
            match(TokKind::Semicolon);
            return s;
        }
        // 否则当作表达式语句 name[idx]
        auto v = std::make_unique<Var>(); v->name = name; v->line = line;
        auto ix = std::make_unique<IndexExpr>();
        ix->line = line; ix->arr = std::move(v); ix->idx = std::move(idx);
        auto es = std::make_unique<ExprStmt>(); es->line = line; es->expr = std::move(ix);
        match(TokKind::Semicolon);
        return es;
    }
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
        case TokKind::Str: {
            auto e = std::make_unique<StringLit>();
            e->line = t.line;
            e->value = t.text;
            advance();
            return e;
        }
        case TokKind::Ident: {
            std::string name = t.text;
            int line = t.line;
            advance();
            ExprPtr e;
            if (check(TokKind::LBrace) && !noStructLit_) {
                // 结构体字面量 Name { f: e, ... }
                auto sl = std::make_unique<StructLit>();
                sl->typeName = name; sl->line = line;
                advance(); // '{'
                bool save = noStructLit_; noStructLit_ = false;
                if (!check(TokKind::RBrace)) {
                    do {
                        FieldInit fi;
                        fi.name = expect(TokKind::Ident, "字段名").text;
                        expect(TokKind::Colon, "':'");
                        fi.value = parseExpr();
                        sl->fields.push_back(std::move(fi));
                    } while (match(TokKind::Comma) && !check(TokKind::RBrace));
                }
                noStructLit_ = save;
                expect(TokKind::RBrace, "'}'");
                e = std::move(sl);
            } else if (check(TokKind::LParen)) {
                auto call = std::make_unique<Call>();
                call->callee = name; call->line = line;
                advance(); // '('
                bool save = noStructLit_; noStructLit_ = false;
                if (!check(TokKind::RParen)) {
                    do { call->args.push_back(parseExpr()); } while (match(TokKind::Comma));
                }
                noStructLit_ = save;
                expect(TokKind::RParen, "')'");
                e = std::move(call);
            } else {
                auto v = std::make_unique<Var>();
                v->name = name; v->line = line;
                e = std::move(v);
            }
            // 后缀：下标 a[i] 与字段 a.f
            while (check(TokKind::LBracket) || check(TokKind::Dot)) {
                if (check(TokKind::LBracket)) {
                    advance();
                    bool save = noStructLit_; noStructLit_ = false;
                    auto ix = std::make_unique<IndexExpr>();
                    ix->line = line; ix->arr = std::move(e); ix->idx = parseExpr();
                    noStructLit_ = save;
                    expect(TokKind::RBracket, "']'");
                    e = std::move(ix);
                } else {
                    advance(); // '.'
                    auto fa = std::make_unique<FieldAccess>();
                    fa->line = line; fa->obj = std::move(e);
                    fa->field = expect(TokKind::Ident, "字段名").text;
                    e = std::move(fa);
                }
            }
            return e;
        }
        case TokKind::LBracket: { // 数组字面量 [e1, e2, ...]
            auto arr = std::make_unique<ArrayLit>();
            arr->line = t.line;
            advance();
            bool save = noStructLit_; noStructLit_ = false;
            if (!check(TokKind::RBrace) && !check(TokKind::RBracket)) {
                do { arr->elems.push_back(parseExpr()); } while (match(TokKind::Comma));
            }
            noStructLit_ = save;
            expect(TokKind::RBracket, "']'");
            return arr;
        }
        case TokKind::LParen: {
            advance();
            bool save = noStructLit_; noStructLit_ = false;
            ExprPtr e = parseExpr();
            noStructLit_ = save;
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
