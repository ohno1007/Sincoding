// blockreader.cpp — 积木模型 JSON → AST（详见 blockreader.h）
#include "blockreader.h"
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace sincoding {
namespace {

// ---------------- 极简 JSON 解析（只覆盖积木模型用到的子集） ----------------
struct JVal;
using JPtr = std::shared_ptr<JVal>;
struct JVal {
    enum K { Null, Bool, Num, Str, Arr, Obj } k = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JPtr> arr;
    std::map<std::string, JPtr> obj;
};

struct JParser {
    const std::string& s;
    size_t i = 0;
    explicit JParser(const std::string& src) : s(src) {}

    void ws() { while (i < s.size() && (s[i]==' '||s[i]=='\n'||s[i]=='\t'||s[i]=='\r')) i++; }
    [[noreturn]] void fail(const std::string& m) { throw std::runtime_error("积木 JSON 解析失败: " + m); }
    char peek() { ws(); if (i >= s.size()) fail("意外结束"); return s[i]; }
    void expect(char c) { if (peek() != c) fail(std::string("期望 '") + c + "'"); i++; }

    JPtr parse() {
        ws();
        char c = peek();
        if (c == '{') return parseObj();
        if (c == '[') return parseArr();
        if (c == '"') { auto v = std::make_shared<JVal>(); v->k = JVal::Str; v->str = parseStr(); return v; }
        if (c == 't' || c == 'f') {
            auto v = std::make_shared<JVal>(); v->k = JVal::Bool;
            if (s.compare(i, 4, "true") == 0) { v->b = true; i += 4; }
            else if (s.compare(i, 5, "false") == 0) { v->b = false; i += 5; }
            else fail("非法字面量");
            return v;
        }
        if (c == 'n') { i += 4; return std::make_shared<JVal>(); }
        return parseNum();
    }
    JPtr parseObj() {
        expect('{');
        auto v = std::make_shared<JVal>(); v->k = JVal::Obj;
        if (peek() == '}') { i++; return v; }
        for (;;) {
            std::string key = parseStr();
            expect(':');
            v->obj[key] = parse();
            char c = peek();
            if (c == ',') { i++; continue; }
            if (c == '}') { i++; break; }
            fail("对象里期望 ',' 或 '}'");
        }
        return v;
    }
    JPtr parseArr() {
        expect('[');
        auto v = std::make_shared<JVal>(); v->k = JVal::Arr;
        if (peek() == ']') { i++; return v; }
        for (;;) {
            v->arr.push_back(parse());
            char c = peek();
            if (c == ',') { i++; continue; }
            if (c == ']') { i++; break; }
            fail("数组里期望 ',' 或 ']'");
        }
        return v;
    }
    std::string parseStr() {
        if (peek() != '"') fail("期望字符串");
        i++;
        std::string out;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                i++;
                switch (s[i]) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {                       // \uXXXX → UTF-8
                        unsigned cp = (unsigned)std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
                        i += 4;
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                        else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: out += s[i];
                }
                i++;
            } else out += s[i++];
        }
        if (i >= s.size()) fail("字符串未闭合");
        i++;
        return out;
    }
    JPtr parseNum() {
        size_t start = i;
        while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i]=='-' || s[i]=='+' ||
                                s[i]=='.' || s[i]=='e' || s[i]=='E')) i++;
        if (start == i) fail("期望数字");
        auto v = std::make_shared<JVal>(); v->k = JVal::Num;
        v->num = std::strtod(s.substr(start, i - start).c_str(), nullptr);
        return v;
    }
};

// ---------------- 取值助手 ----------------
const JPtr kNull;
JPtr get(const JPtr& o, const char* key) {
    if (!o || o->k != JVal::Obj) return nullptr;
    auto it = o->obj.find(key);
    return it == o->obj.end() ? nullptr : it->second;
}
std::string gstr(const JPtr& o, const char* key, const std::string& def = "") {
    auto v = get(o, key);
    return (v && v->k == JVal::Str) ? v->str : def;
}
double gnum(const JPtr& o, const char* key, double def = 0) {
    auto v = get(o, key);
    return (v && v->k == JVal::Num) ? v->num : def;
}
bool gbool(const JPtr& o, const char* key, bool def = false) {
    auto v = get(o, key);
    return (v && v->k == JVal::Bool) ? v->b : def;
}

// 注释字段回读："pre" 字符串数组 + "tail" 字符串（serializeBlocks 的 commentFields 对应物）
void gcomments(const JPtr& o, std::vector<std::string>& pre, std::string& tail) {
    auto p = get(o, "pre");
    if (p && p->k == JVal::Arr)
        for (auto& c : p->arr) if (c->k == JVal::Str) pre.push_back(c->str);
    tail = gstr(o, "tail");
}

// 类型名 → (Type, structName)。非内置名一律当结构体名（也可能是泛型类型参数）
void parseTypeName(const std::string& n, Type& t, std::string& sn) {
    sn.clear();
    if (n == "int") t = Type::Int;
    else if (n == "float") t = Type::Float;
    else if (n == "bool") t = Type::Bool;
    else if (n == "string") t = Type::String;
    else if (n == "void") t = Type::Void;
    else if (n.empty() || n == "<unknown>") t = Type::Unknown;
    else { t = Type::Struct; sn = n; }
}

// ---------------- 积木 JSON → AST ----------------
ExprPtr readExpr(const JPtr& j);
StmtPtr readStmt(const JPtr& j);

template <typename T>
std::unique_ptr<T> mk(const JPtr& j) {
    auto n = std::make_unique<T>();
    n->line = (int)gnum(j, "line", 0);
    n->col = (int)gnum(j, "col", 0);
    return n;
}

ExprPtr readExpr(const JPtr& j) {
    std::string b = gstr(j, "block");
    if (b == "int")   { auto n = mk<IntLit>(j);   n->value = (long long)gnum(j, "value"); return n; }
    if (b == "float") { auto n = mk<FloatLit>(j); n->value = gnum(j, "value"); return n; }
    if (b == "bool")  { auto n = mk<BoolLit>(j);  n->value = gbool(j, "value"); return n; }
    if (b == "string"){ auto n = mk<StringLit>(j);n->value = gstr(j, "value"); return n; }
    if (b == "var")   { auto n = mk<Var>(j);      n->name = gstr(j, "name"); return n; }
    if (b == "unary") {
        auto n = mk<Unary>(j); n->op = gstr(j, "op"); n->operand = readExpr(get(j, "operand")); return n;
    }
    if (b == "binary") {
        auto n = mk<Binary>(j); n->op = gstr(j, "op");
        n->lhs = readExpr(get(j, "lhs")); n->rhs = readExpr(get(j, "rhs")); return n;
    }
    if (b == "call") {
        auto n = mk<Call>(j); n->callee = gstr(j, "callee");
        auto args = get(j, "args");
        if (args && args->k == JVal::Arr) for (auto& a : args->arr) n->args.push_back(readExpr(a));
        return n;
    }
    if (b == "index") {
        auto n = mk<IndexExpr>(j); n->arr = readExpr(get(j, "arr")); n->idx = readExpr(get(j, "idx")); return n;
    }
    if (b == "array") {
        auto n = mk<ArrayLit>(j);
        auto es = get(j, "elems");
        if (es && es->k == JVal::Arr) for (auto& e : es->arr) n->elems.push_back(readExpr(e));
        return n;
    }
    if (b == "field") {
        auto n = mk<FieldAccess>(j); n->obj = readExpr(get(j, "obj")); n->field = gstr(j, "name"); return n;
    }
    if (b == "structlit") {
        auto n = mk<StructLit>(j); n->typeName = gstr(j, "typeName");
        auto fs = get(j, "fields");
        if (fs && fs->k == JVal::Arr)
            for (auto& f : fs->arr) {
                FieldInit fi; fi.name = gstr(f, "name"); fi.value = readExpr(get(f, "value"));
                n->fields.push_back(std::move(fi));
            }
        return n;
    }
    // 未知块：**硬报错**。静默改写成 0 会在下一次写回时把用户数据毁掉还显示"已同步"
    throw std::runtime_error("未知积木类型: " + (b.empty() ? "(空)" : b));
}

BlockPtr readBlockList(const JPtr& arr) {
    auto blk = std::make_unique<Block>();
    if (arr && arr->k == JVal::Arr) for (auto& s : arr->arr) blk->stmts.push_back(readStmt(s));
    return blk;
}

StmtPtr readStmtInner(const JPtr& j);
StmtPtr readStmt(const JPtr& j) {
    auto n = readStmtInner(j);
    if (n) gcomments(j, n->preComments, n->tailComment);
    return n;
}
StmtPtr readStmtInner(const JPtr& j) {
    std::string b = gstr(j, "block");
    if (b == "let") {
        auto n = mk<LetStmt>(j); n->name = gstr(j, "name");
        parseTypeName(gstr(j, "type"), n->declared, n->structName);
        n->declaredLen = (int)gnum(j, "len", 0);
        if (get(j, "value")) n->init = readExpr(get(j, "value"));
        return n;
    }
    if (b == "assign") {
        auto n = mk<AssignStmt>(j); n->name = gstr(j, "name"); n->field = gstr(j, "field");
        if (get(j, "index")) n->index = readExpr(get(j, "index"));
        n->value = readExpr(get(j, "value"));
        return n;
    }
    if (b == "if") {
        auto n = mk<IfStmt>(j); n->cond = readExpr(get(j, "cond"));
        n->thenBlock = readBlockList(get(j, "then"));
        if (get(j, "else")) n->elseBlock = readBlockList(get(j, "else"));
        return n;
    }
    if (b == "while") {
        auto n = mk<WhileStmt>(j); n->cond = readExpr(get(j, "cond"));
        n->body = readBlockList(get(j, "body")); return n;
    }
    if (b == "for") {
        auto n = mk<ForStmt>(j); n->var = gstr(j, "var");
        n->start = readExpr(get(j, "start")); n->end = readExpr(get(j, "end"));
        n->body = readBlockList(get(j, "body")); return n;
    }
    if (b == "return") {
        auto n = mk<ReturnStmt>(j);
        if (get(j, "value")) n->value = readExpr(get(j, "value"));
        return n;
    }
    if (b == "block_group") return readBlockList(get(j, "body"));
    if (b == "expr") {
        auto n = mk<ExprStmt>(j);
        n->expr = get(j, "expr") ? readExpr(get(j, "expr")) : readExpr(j);
        return n;
    }
    throw std::runtime_error("未知语句积木类型: " + (b.empty() ? "(空)" : b));
}

FnPtr readFn(const JPtr& j) {
    auto fn = std::make_unique<FnDecl>();
    fn->name = gstr(j, "name");
    fn->isExtern = (gstr(j, "block") == "extern_fn");
    auto tps = get(j, "typeParams");
    if (tps && tps->k == JVal::Arr)
        for (auto& t : tps->arr) if (t->k == JVal::Str) fn->typeParams.push_back(t->str);
    auto ps = get(j, "params");
    if (ps && ps->k == JVal::Arr)
        for (auto& p : ps->arr) {
            Param pa;
            pa.name = gstr(p, "name");
            parseTypeName(gstr(p, "type"), pa.type, pa.structName);
            pa.len = (int)gnum(p, "len", 0);
            pa.line = 0;
            fn->params.push_back(pa);
        }
    parseTypeName(gstr(j, "ret", "void"), fn->ret, fn->retStruct);
    fn->retLen = (int)gnum(j, "retLen", 0);
    if (!fn->isExtern) fn->body = readBlockList(get(j, "body"));
    gcomments(j, fn->preComments, fn->tailComment);
    return fn;
}

} // namespace

Program blocksToProgram(const std::string& json, bool& ok, std::string& err) {
    Program prog;
    ok = true; err.clear();
    try {
        JParser jp(json);
        JPtr root = jp.parse();

        auto imports = get(root, "imports");
        if (imports && imports->k == JVal::Arr)
            for (auto& im : imports->arr)
                if (im->k == JVal::Str) { ImportDecl d; d.name = im->str; prog.imports.push_back(d); }
        // import 行的注释（与 imports 对齐的数组；文件头注释挂在第一个 import 上）
        auto ipre = get(root, "importsPre");
        if (ipre && ipre->k == JVal::Arr)
            for (size_t i = 0; i < ipre->arr.size() && i < prog.imports.size(); i++)
                if (ipre->arr[i]->k == JVal::Arr)
                    for (auto& c : ipre->arr[i]->arr)
                        if (c->k == JVal::Str) prog.imports[i].preComments.push_back(c->str);
        auto itail = get(root, "importsTail");
        if (itail && itail->k == JVal::Arr)
            for (size_t i = 0; i < itail->arr.size() && i < prog.imports.size(); i++)
                if (itail->arr[i]->k == JVal::Str) prog.imports[i].tailComment = itail->arr[i]->str;

        auto structs = get(root, "structs");
        if (structs && structs->k == JVal::Arr)
            for (auto& st : structs->arr) {
                auto sd = std::make_unique<StructDecl>();
                sd->name = gstr(st, "name");
                auto fs = get(st, "fields");
                if (fs && fs->k == JVal::Arr)
                    for (auto& f : fs->arr) {
                        StructField sf;
                        sf.name = gstr(f, "name");
                        parseTypeName(gstr(f, "type"), sf.type, sf.structName);
                        sf.len = (int)gnum(f, "len", 0);
                        sf.line = 0;
                        gcomments(f, sf.preComments, sf.tailComment);
                        sd->fields.push_back(sf);
                    }
                gcomments(st, sd->preComments, sd->tailComment);
                prog.structs.push_back(std::move(sd));
            }

        auto globals = get(root, "globals");
        if (globals && globals->k == JVal::Arr)
            for (auto& g : globals->arr) prog.globals.push_back(readStmt(g));

        auto program = get(root, "program");
        if (program && program->k == JVal::Arr)
            for (auto& f : program->arr) prog.fns.push_back(readFn(f));

        auto tails = get(root, "tailComments");
        if (tails && tails->k == JVal::Arr)
            for (auto& c : tails->arr)
                if (c->k == JVal::Str) prog.tailComments.push_back(c->str);
    } catch (const std::exception& e) {
        ok = false; err = e.what();
    }
    return prog;
}

} // namespace sincoding
