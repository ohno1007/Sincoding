// token.h — Sincoding 语言的词法单元定义
#pragma once
#include <string>

namespace sincoding {

enum class TokKind {
    // 字面量 / 标识符
    Int, Float, Str, Ident,
    // 关键字
    KwLet, KwFn, KwIf, KwElse, KwWhile, KwReturn,
    KwExtern, KwTrue, KwFalse,
    KwTypeInt, KwTypeFloat, KwTypeBool, KwTypeVoid, KwTypeString,
    // 运算符
    Plus, Minus, Star, Slash, Percent,
    Assign,                 // =
    Eq, Ne, Lt, Le, Gt, Ge, // == != < <= > >=
    AndAnd, OrOr, Not,      // && || !
    Arrow,                  // ->
    // 标点
    LParen, RParen, LBrace, RBrace,
    Comma, Colon, Semicolon,
    // 结束
    End,
};

struct Token {
    TokKind kind;
    std::string text;   // 原文（标识符 / 数字）
    int line;
    int col;
};

const char* tokKindName(TokKind k);

} // namespace sincoding
