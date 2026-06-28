#include "lexer.h"
#include <cctype>
#include <unordered_map>

namespace sincoding {

const char* tokKindName(TokKind k) {
    switch (k) {
        case TokKind::Int: return "int-literal";
        case TokKind::Float: return "float-literal";
        case TokKind::Str: return "string-literal";
        case TokKind::Ident: return "identifier";
        case TokKind::KwLet: return "let";
        case TokKind::KwFn: return "fn";
        case TokKind::KwIf: return "if";
        case TokKind::KwElse: return "else";
        case TokKind::KwWhile: return "while";
        case TokKind::KwFor: return "for";
        case TokKind::KwReturn: return "return";
        case TokKind::KwExtern: return "extern";
        case TokKind::KwStruct: return "struct";
        case TokKind::KwTrue: return "true";
        case TokKind::KwFalse: return "false";
        case TokKind::KwTypeInt: return "int";
        case TokKind::KwTypeFloat: return "float";
        case TokKind::KwTypeBool: return "bool";
        case TokKind::KwTypeVoid: return "void";
        case TokKind::KwTypeString: return "string";
        case TokKind::Plus: return "+";
        case TokKind::Minus: return "-";
        case TokKind::Star: return "*";
        case TokKind::Slash: return "/";
        case TokKind::Percent: return "%";
        case TokKind::Assign: return "=";
        case TokKind::Eq: return "==";
        case TokKind::Ne: return "!=";
        case TokKind::Lt: return "<";
        case TokKind::Le: return "<=";
        case TokKind::Gt: return ">";
        case TokKind::Ge: return ">=";
        case TokKind::AndAnd: return "&&";
        case TokKind::OrOr: return "||";
        case TokKind::Not: return "!";
        case TokKind::Arrow: return "->";
        case TokKind::Dot: return ".";
        case TokKind::DotDot: return "..";
        case TokKind::LParen: return "(";
        case TokKind::RParen: return ")";
        case TokKind::LBrace: return "{";
        case TokKind::RBrace: return "}";
        case TokKind::LBracket: return "[";
        case TokKind::RBracket: return "]";
        case TokKind::Comma: return ",";
        case TokKind::Colon: return ":";
        case TokKind::Semicolon: return ";";
        case TokKind::End: return "<eof>";
    }
    return "?";
}

static const std::unordered_map<std::string, TokKind>& keywords() {
    static const std::unordered_map<std::string, TokKind> kw = {
        {"let", TokKind::KwLet},       {"fn", TokKind::KwFn},
        {"if", TokKind::KwIf},         {"else", TokKind::KwElse},
        {"while", TokKind::KwWhile},   {"for", TokKind::KwFor},
        {"return", TokKind::KwReturn}, {"extern", TokKind::KwExtern},
        {"struct", TokKind::KwStruct},
        {"true", TokKind::KwTrue},     {"false", TokKind::KwFalse},
        {"int", TokKind::KwTypeInt},   {"float", TokKind::KwTypeFloat},
        {"bool", TokKind::KwTypeBool}, {"void", TokKind::KwTypeVoid},
        {"string", TokKind::KwTypeString},
    };
    return kw;
}

char Lexer::peek(int ahead) const {
    int p = pos_ + ahead;
    if (p < 0 || p >= (int)src_.size()) return '\0';
    return src_[p];
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { line_++; col_ = 1; }
    else { col_++; }
    return c;
}

bool Lexer::match(char expected) {
    if (atEnd() || src_[pos_] != expected) return false;
    advance();
    return true;
}

void Lexer::addToken(TokKind kind, std::string text) {
    tokens_.push_back({kind, std::move(text), line_, tokStartCol_});
}

void Lexer::error(const std::string& msg) {
    errors_.push_back({line_, col_, msg});
}

void Lexer::lexNumber() {
    std::string num;
    while (std::isdigit((unsigned char)peek())) num += advance();
    bool isFloat = false;
    // 小数部分：必须 '.' 后跟数字，避免吃掉成员访问等
    if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
        isFloat = true;
        num += advance(); // '.'
        while (std::isdigit((unsigned char)peek())) num += advance();
    }
    addToken(isFloat ? TokKind::Float : TokKind::Int, num);
}

void Lexer::lexString() {
    advance(); // 消费开引号 "
    std::string val;
    while (!atEnd() && peek() != '"') {
        char c = advance();
        if (c == '\\' && !atEnd()) {
            char e = advance();
            switch (e) {
                case 'n': val += '\n'; break;
                case 't': val += '\t'; break;
                case 'r': val += '\r'; break;
                case '"': val += '"'; break;
                case '\\': val += '\\'; break;
                case '0': val += '\0'; break;
                default: val += e; break; // 未知转义按原字符
            }
        } else if (c == '\n') {
            error("字符串字面量不能跨行");
            break;
        } else {
            val += c;
        }
    }
    if (atEnd() || peek() != '"') { error("字符串缺少结束引号 '\"'"); }
    else advance(); // 消费闭引号
    addToken(TokKind::Str, val);
}

void Lexer::lexIdentOrKeyword() {
    std::string id;
    while (std::isalnum((unsigned char)peek()) || peek() == '_') id += advance();
    auto it = keywords().find(id);
    if (it != keywords().end()) addToken(it->second, id);
    else addToken(TokKind::Ident, id);
}

std::vector<Token> Lexer::tokenize() {
    while (!atEnd()) {
        tokStartCol_ = col_;
        char c = peek();

        // 空白
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }

        // 行注释 //
        if (c == '/' && peek(1) == '/') {
            while (!atEnd() && peek() != '\n') advance();
            continue;
        }

        // 字符串字面量
        if (c == '"') { lexString(); continue; }

        // 数字
        if (std::isdigit((unsigned char)c)) { lexNumber(); continue; }

        // 标识符 / 关键字
        if (std::isalpha((unsigned char)c) || c == '_') { lexIdentOrKeyword(); continue; }

        advance(); // 消费当前字符
        switch (c) {
            case '+': addToken(TokKind::Plus, "+"); break;
            case '*': addToken(TokKind::Star, "*"); break;
            case '/': addToken(TokKind::Slash, "/"); break;
            case '%': addToken(TokKind::Percent, "%"); break;
            case '(': addToken(TokKind::LParen, "("); break;
            case ')': addToken(TokKind::RParen, ")"); break;
            case '{': addToken(TokKind::LBrace, "{"); break;
            case '}': addToken(TokKind::RBrace, "}"); break;
            case '[': addToken(TokKind::LBracket, "["); break;
            case ']': addToken(TokKind::RBracket, "]"); break;
            case ',': addToken(TokKind::Comma, ","); break;
            case '.':
                if (match('.')) addToken(TokKind::DotDot, "..");
                else addToken(TokKind::Dot, ".");   // 字段访问 p.x
                break;
            case ':': addToken(TokKind::Colon, ":"); break;
            case ';': addToken(TokKind::Semicolon, ";"); break;
            case '-':
                if (match('>')) addToken(TokKind::Arrow, "->");
                else addToken(TokKind::Minus, "-");
                break;
            case '=':
                if (match('=')) addToken(TokKind::Eq, "==");
                else addToken(TokKind::Assign, "=");
                break;
            case '!':
                if (match('=')) addToken(TokKind::Ne, "!=");
                else addToken(TokKind::Not, "!");
                break;
            case '<':
                if (match('=')) addToken(TokKind::Le, "<=");
                else addToken(TokKind::Lt, "<");
                break;
            case '>':
                if (match('=')) addToken(TokKind::Ge, ">=");
                else addToken(TokKind::Gt, ">");
                break;
            case '&':
                if (match('&')) addToken(TokKind::AndAnd, "&&");
                else error("意外的字符 '&'（是否想写 '&&'？）");
                break;
            case '|':
                if (match('|')) addToken(TokKind::OrOr, "||");
                else error("意外的字符 '|'（是否想写 '||'？）");
                break;
            default:
                error(std::string("意外的字符 '") + c + "'");
                break;
        }
    }
    tokStartCol_ = col_;
    addToken(TokKind::End, "");
    return tokens_;
}

} // namespace sincoding
