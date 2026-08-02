// query.cpp — IDE 查询实现（详见 query.h）
#include "query.h"
#include "serializer.h"
#include <string>
#include <vector>

namespace sincoding {
namespace {

std::string jesc(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    return o + "\"";
}

// 表达式的类型描述（int / int[5] / Point / Point[3]）
std::string typeDesc(const Expr& e) {
    std::string base = (e.type == Type::Struct)
        ? (e.structName.empty() ? "struct" : e.structName)
        : typeName(e.type);
    if (e.arrayLen > 0) base += "[" + std::to_string(e.arrayLen) + "]";
    return base;
}

// 该表达式若是一个「标识符」，返回其文本长度，否则 0
int identLen(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Var:       return (int)static_cast<const Var&>(e).name.size();
        case ExprKind::Call:      return (int)static_cast<const Call&>(e).callee.size();
        case ExprKind::Field:     return (int)static_cast<const FieldAccess&>(e).field.size();
        case ExprKind::StructLit: return (int)static_cast<const StructLit&>(e).typeName.size();
        default: return 0;
    }
}
std::string identName(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Var:       return static_cast<const Var&>(e).name;
        case ExprKind::Call:      return static_cast<const Call&>(e).callee;
        case ExprKind::Field:     return static_cast<const FieldAccess&>(e).field;
        case ExprKind::StructLit: return static_cast<const StructLit&>(e).typeName;
        default: return "";
    }
}
const char* identKind(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Var:       return "变量";
        case ExprKind::Call:      return "函数";
        case ExprKind::Field:     return "字段";
        case ExprKind::StructLit: return "结构体";
        default: return "";
    }
}

// 在表达式子树里找覆盖 (ql,qc) 的最内层标识符
const Expr* exprAt(const Expr& e, int ql, int qc) {
    const Expr* r = nullptr;
    switch (e.kind) {
        case ExprKind::Unary: r = exprAt(*static_cast<const Unary&>(e).operand, ql, qc); break;
        case ExprKind::Binary: {
            auto& b = static_cast<const Binary&>(e);
            r = exprAt(*b.lhs, ql, qc); if (!r) r = exprAt(*b.rhs, ql, qc);
            break;
        }
        case ExprKind::Call: {
            for (auto& a : static_cast<const Call&>(e).args) { r = exprAt(*a, ql, qc); if (r) break; }
            break;
        }
        case ExprKind::Index: {
            auto& x = static_cast<const IndexExpr&>(e);
            r = exprAt(*x.arr, ql, qc); if (!r) r = exprAt(*x.idx, ql, qc);
            break;
        }
        case ExprKind::ArrayLit:
            for (auto& el : static_cast<const ArrayLit&>(e).elems) { r = exprAt(*el, ql, qc); if (r) break; }
            break;
        case ExprKind::Field: r = exprAt(*static_cast<const FieldAccess&>(e).obj, ql, qc); break;
        case ExprKind::StructLit:
            for (auto& fi : static_cast<const StructLit&>(e).fields) { r = exprAt(*fi.value, ql, qc); if (r) break; }
            break;
        default: break;
    }
    if (r) return r;
    int len = identLen(e);
    if (len > 0 && e.line == ql && e.col <= qc && qc < e.col + len) return &e;
    return nullptr;
}

const Expr* stmtAt(const Stmt& s, int ql, int qc);
const Expr* blockAt(const Block& b, int ql, int qc) {
    for (auto& s : b.stmts) if (const Expr* r = stmtAt(*s, ql, qc)) return r;
    return nullptr;
}
const Expr* stmtAt(const Stmt& s, int ql, int qc) {
    switch (s.kind) {
        case StmtKind::Let: { auto& l = static_cast<const LetStmt&>(s); return l.init ? exprAt(*l.init, ql, qc) : nullptr; }
        case StmtKind::Assign: {
            auto& a = static_cast<const AssignStmt&>(s);
            if (a.index) if (const Expr* r = exprAt(*a.index, ql, qc)) return r;
            return exprAt(*a.value, ql, qc);
        }
        case StmtKind::If: {
            auto& i = static_cast<const IfStmt&>(s);
            if (const Expr* r = exprAt(*i.cond, ql, qc)) return r;
            if (const Expr* r = blockAt(*i.thenBlock, ql, qc)) return r;
            return i.elseBlock ? blockAt(*i.elseBlock, ql, qc) : nullptr;
        }
        case StmtKind::While: {
            auto& w = static_cast<const WhileStmt&>(s);
            if (const Expr* r = exprAt(*w.cond, ql, qc)) return r;
            return blockAt(*w.body, ql, qc);
        }
        case StmtKind::For: {
            auto& f = static_cast<const ForStmt&>(s);
            if (const Expr* r = exprAt(*f.start, ql, qc)) return r;
            if (const Expr* r = exprAt(*f.end, ql, qc)) return r;
            return blockAt(*f.body, ql, qc);
        }
        case StmtKind::Return: { auto& r = static_cast<const ReturnStmt&>(s); return r.value ? exprAt(*r.value, ql, qc) : nullptr; }
        case StmtKind::ExprStmt: return exprAt(*static_cast<const ExprStmt&>(s).expr, ql, qc);
        case StmtKind::Block: return blockAt(static_cast<const Block&>(s), ql, qc);
    }
    return nullptr;
}

const Expr* progAt(const Program& prog, int ql, int qc) {
    for (auto& g : prog.globals) if (const Expr* r = stmtAt(*g, ql, qc)) return r;
    for (auto& fn : prog.fns) if (fn->body) if (const Expr* r = blockAt(*fn->body, ql, qc)) return r;
    return nullptr;
}

} // namespace

std::string queryHover(const Program& prog, int line, int col) {
    const Expr* hit = progAt(prog, line, col);
    if (!hit) return "{\"found\":false}";
    return "{\"found\":true,\"name\":" + jesc(identName(*hit)) +
           ",\"kind\":" + jesc(identKind(*hit)) +
           ",\"type\":" + jesc(typeDesc(*hit)) + "}";
}

// ============================ 查找引用 / 重命名 ============================
namespace {

// 一个标识符出现（定义或引用）。cat: 'v'ar 'f'n 's'truct 'F'ield
struct Occ { int line, col, len; std::string name; char cat; int fnIndex; };

void addOcc(std::vector<Occ>& out, int line, int col, const std::string& name, char cat, int fn) {
    if (!name.empty() && col > 0) out.push_back({line, col, (int)name.size(), name, cat, fn});
}

void occExpr(const Expr& e, int fn, std::vector<Occ>& out) {
    switch (e.kind) {
        case ExprKind::Var: { auto& v = static_cast<const Var&>(e); addOcc(out, v.line, v.col, v.name, 'v', fn); break; }
        case ExprKind::Call: {
            auto& c = static_cast<const Call&>(e);
            addOcc(out, c.line, c.col, c.callee, 'f', fn);
            for (auto& a : c.args) occExpr(*a, fn, out);
            break;
        }
        case ExprKind::Field: {
            auto& fa = static_cast<const FieldAccess&>(e);
            occExpr(*fa.obj, fn, out);
            addOcc(out, fa.line, fa.col, fa.field, 'F', fn);
            break;
        }
        case ExprKind::StructLit: {
            auto& sl = static_cast<const StructLit&>(e);
            addOcc(out, sl.line, sl.col, sl.typeName, 's', fn);
            for (auto& fi : sl.fields) occExpr(*fi.value, fn, out);
            break;
        }
        case ExprKind::Unary: occExpr(*static_cast<const Unary&>(e).operand, fn, out); break;
        case ExprKind::Binary: { auto& b = static_cast<const Binary&>(e); occExpr(*b.lhs, fn, out); occExpr(*b.rhs, fn, out); break; }
        case ExprKind::Index: { auto& x = static_cast<const IndexExpr&>(e); occExpr(*x.arr, fn, out); occExpr(*x.idx, fn, out); break; }
        case ExprKind::ArrayLit: for (auto& el : static_cast<const ArrayLit&>(e).elems) occExpr(*el, fn, out); break;
        default: break;
    }
}
void occStmt(const Stmt& s, int fn, std::vector<Occ>& out);
void occBlock(const Block& b, int fn, std::vector<Occ>& out) { for (auto& s : b.stmts) occStmt(*s, fn, out); }
void occStmt(const Stmt& s, int fn, std::vector<Occ>& out) {
    switch (s.kind) {
        case StmtKind::Let: { auto& l = static_cast<const LetStmt&>(s); addOcc(out, l.line, l.col, l.name, 'v', fn); if (l.init) occExpr(*l.init, fn, out); break; }
        case StmtKind::Assign: {
            auto& a = static_cast<const AssignStmt&>(s);
            addOcc(out, a.line, a.col, a.name, 'v', fn);
            if (a.index) occExpr(*a.index, fn, out);
            occExpr(*a.value, fn, out);
            break;
        }
        case StmtKind::If: { auto& i = static_cast<const IfStmt&>(s); occExpr(*i.cond, fn, out); occBlock(*i.thenBlock, fn, out); if (i.elseBlock) occBlock(*i.elseBlock, fn, out); break; }
        case StmtKind::While: { auto& w = static_cast<const WhileStmt&>(s); occExpr(*w.cond, fn, out); occBlock(*w.body, fn, out); break; }
        case StmtKind::For: { auto& f = static_cast<const ForStmt&>(s); addOcc(out, f.line, f.col, f.var, 'v', fn); occExpr(*f.start, fn, out); occExpr(*f.end, fn, out); occBlock(*f.body, fn, out); break; }
        case StmtKind::Return: { auto& r = static_cast<const ReturnStmt&>(s); if (r.value) occExpr(*r.value, fn, out); break; }
        case StmtKind::ExprStmt: occExpr(*static_cast<const ExprStmt&>(s).expr, fn, out); break;
        case StmtKind::Block: occBlock(static_cast<const Block&>(s), fn, out); break;
    }
}
std::vector<Occ> collectOccurrences(const Program& prog) {
    std::vector<Occ> out;
    for (auto& st : prog.structs) {
        addOcc(out, st->line, st->col, st->name, 's', -1);
        for (auto& f : st->fields) addOcc(out, f.line, f.col, f.name, 'F', -1);
    }
    for (size_t i = 0; i < prog.fns.size(); i++) {
        auto& fn = *prog.fns[i];
        addOcc(out, fn.line, fn.col, fn.name, 'f', -1);
        for (auto& p : fn.params) addOcc(out, p.line, p.col, p.name, 'v', (int)i);
        if (fn.body) occBlock(*fn.body, (int)i, out);
    }
    for (auto& g : prog.globals) occStmt(*g, -1, out);
    return out;
}
const Occ* occAt(const std::vector<Occ>& occs, int ql, int qc) {
    for (auto& o : occs) if (o.line == ql && o.col <= qc && qc < o.col + o.len) return &o;
    return nullptr;
}
// 变量作用域匹配：局部（fnIndex>=0）限同函数；全局（-1）宽松匹配同名（近似）
bool sameVarScope(const Occ& a, const Occ& b) {
    if (a.fnIndex < 0) return true;          // 全局变量：近似匹配所有同名
    return a.fnIndex == b.fnIndex;           // 局部/参数：限同函数
}

// ---- 重命名：直接改 AST 字段，再由 serializeSource 输出 ----
void renExpr(Expr& e, char cat, const std::string& from, const std::string& to) {
    switch (e.kind) {
        case ExprKind::Var: if (cat == 'v') { auto& v = static_cast<Var&>(e); if (v.name == from) v.name = to; } break;
        case ExprKind::Call: { auto& c = static_cast<Call&>(e); if (cat == 'f' && c.callee == from) c.callee = to; for (auto& a : c.args) renExpr(*a, cat, from, to); break; }
        case ExprKind::Field: { auto& fa = static_cast<FieldAccess&>(e); renExpr(*fa.obj, cat, from, to); if (cat == 'F' && fa.field == from) fa.field = to; break; }
        case ExprKind::StructLit: { auto& sl = static_cast<StructLit&>(e); if (cat == 's' && sl.typeName == from) sl.typeName = to; for (auto& fi : sl.fields) { if (cat == 'F' && fi.name == from) fi.name = to; renExpr(*fi.value, cat, from, to); } break; }
        case ExprKind::Unary: renExpr(*static_cast<Unary&>(e).operand, cat, from, to); break;
        case ExprKind::Binary: { auto& b = static_cast<Binary&>(e); renExpr(*b.lhs, cat, from, to); renExpr(*b.rhs, cat, from, to); break; }
        case ExprKind::Index: { auto& x = static_cast<IndexExpr&>(e); renExpr(*x.arr, cat, from, to); renExpr(*x.idx, cat, from, to); break; }
        case ExprKind::ArrayLit: for (auto& el : static_cast<ArrayLit&>(e).elems) renExpr(*el, cat, from, to); break;
        default: break;
    }
}
void renStmt(Stmt& s, char cat, const std::string& from, const std::string& to);
void renBlock(Block& b, char cat, const std::string& from, const std::string& to) { for (auto& s : b.stmts) renStmt(*s, cat, from, to); }
void renStmt(Stmt& s, char cat, const std::string& from, const std::string& to) {
    switch (s.kind) {
        case StmtKind::Let: { auto& l = static_cast<LetStmt&>(s); if (cat == 'v' && l.name == from) l.name = to; if (cat == 's' && l.structName == from) l.structName = to; if (l.init) renExpr(*l.init, cat, from, to); break; }
        case StmtKind::Assign: { auto& a = static_cast<AssignStmt&>(s); if (cat == 'v' && a.name == from) a.name = to; if (cat == 'F' && a.field == from) a.field = to; if (a.index) renExpr(*a.index, cat, from, to); renExpr(*a.value, cat, from, to); break; }
        case StmtKind::If: { auto& i = static_cast<IfStmt&>(s); renExpr(*i.cond, cat, from, to); renBlock(*i.thenBlock, cat, from, to); if (i.elseBlock) renBlock(*i.elseBlock, cat, from, to); break; }
        case StmtKind::While: { auto& w = static_cast<WhileStmt&>(s); renExpr(*w.cond, cat, from, to); renBlock(*w.body, cat, from, to); break; }
        case StmtKind::For: { auto& f = static_cast<ForStmt&>(s); if (cat == 'v' && f.var == from) f.var = to; renExpr(*f.start, cat, from, to); renExpr(*f.end, cat, from, to); renBlock(*f.body, cat, from, to); break; }
        case StmtKind::Return: { auto& r = static_cast<ReturnStmt&>(s); if (r.value) renExpr(*r.value, cat, from, to); break; }
        case StmtKind::ExprStmt: renExpr(*static_cast<ExprStmt&>(s).expr, cat, from, to); break;
        case StmtKind::Block: renBlock(static_cast<Block&>(s), cat, from, to); break;
    }
}
void renameProgram(Program& prog, char cat, const std::string& from, const std::string& to, int fnIndex) {
    if (cat == 's') { // 结构体名：全局，改所有 structName 标注
        for (auto& st : prog.structs) { if (st->name == from) st->name = to; }
        for (auto& fn : prog.fns) {
            if (fn->retStruct == from) fn->retStruct = to;
            for (auto& p : fn->params) if (p.structName == from) p.structName = to;
            if (fn->body) renBlock(*fn->body, cat, from, to);
        }
        for (auto& g : prog.globals) renStmt(*g, cat, from, to);
    } else if (cat == 'F') { // 字段名：全局同名（近似）
        for (auto& st : prog.structs) for (auto& f : st->fields) if (f.name == from) f.name = to;
        for (auto& fn : prog.fns) if (fn->body) renBlock(*fn->body, cat, from, to);
        for (auto& g : prog.globals) renStmt(*g, cat, from, to);
    } else if (cat == 'f') { // 函数名：全局
        for (auto& fn : prog.fns) { if (fn->name == from) fn->name = to; if (fn->body) renBlock(*fn->body, cat, from, to); }
        for (auto& g : prog.globals) renStmt(*g, cat, from, to);
    } else { // 'v' 变量
        if (fnIndex >= 0 && fnIndex < (int)prog.fns.size()) { // 局部/参数：限该函数
            auto& fn = *prog.fns[fnIndex];
            for (auto& p : fn.params) if (p.name == from) p.name = to;
            if (fn.body) renBlock(*fn.body, cat, from, to);
        } else { // 全局变量：globals + 所有函数体引用（近似）
            for (auto& g : prog.globals) renStmt(*g, cat, from, to);
            for (auto& fn : prog.fns) if (fn->body) renBlock(*fn->body, cat, from, to);
        }
    }
}

const char* catName(char c) {
    switch (c) { case 'v': return "变量"; case 'f': return "函数"; case 's': return "结构体"; case 'F': return "字段"; default: return ""; }
}

} // namespace

std::string queryReferences(const Program& prog, int line, int col) {
    auto occs = collectOccurrences(prog);
    const Occ* hit = occAt(occs, line, col);
    if (!hit) return "{\"found\":false}";
    std::string refs = "[";
    bool first = true;
    for (auto& o : occs) {
        if (o.cat != hit->cat || o.name != hit->name) continue;
        if (o.cat == 'v' && !sameVarScope(*hit, o)) continue;
        if (!first) refs += ",";
        first = false;
        refs += "{\"line\":" + std::to_string(o.line) + ",\"col\":" + std::to_string(o.col) +
                ",\"len\":" + std::to_string(o.len) + "}";
    }
    refs += "]";
    return "{\"found\":true,\"name\":" + jesc(hit->name) + ",\"kind\":" + jesc(catName(hit->cat)) +
           ",\"refs\":" + refs + "}";
}

std::string applyRename(Program& prog, int line, int col, const std::string& newName) {
    auto occs = collectOccurrences(prog);
    const Occ* hit = occAt(occs, line, col);
    if (!hit) return "{\"ok\":false,\"note\":\"光标处不是可改名的标识符\"}";
    char cat = hit->cat;
    std::string from = hit->name;
    int fnIndex = hit->fnIndex;
    const char* note = (cat == 'v' && fnIndex < 0) ? "全局变量：跨函数按同名近似替换，请检查"
                     : (cat == 'F') ? "字段：按同名近似替换（可能涉及多个结构体），请检查"
                     : "";
    renameProgram(prog, cat, from, newName, fnIndex);
    return "{\"ok\":true,\"source\":" + jesc(serializeSource(prog)) + ",\"note\":" + jesc(note) + "}";
}

} // namespace sincoding
