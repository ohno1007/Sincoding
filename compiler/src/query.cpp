// query.cpp — IDE 查询实现（详见 query.h）
#include "query.h"
#include <string>

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

} // namespace sincoding
