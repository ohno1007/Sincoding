#include "serializer.h"
#include <sstream>

namespace sincoding {

// ======================================================================
//  AST → Sincoding 源码
// ======================================================================
namespace {

struct SourceWriter {
    std::ostringstream out;
    int depth = 0;

    void indent() { for (int i = 0; i < depth; i++) out << "    "; }

    void writeExpr(const Expr& e) {
        switch (e.kind) {
            case ExprKind::IntLit:
                out << static_cast<const IntLit&>(e).value;
                break;
            case ExprKind::FloatLit: {
                std::ostringstream t;
                t << static_cast<const FloatLit&>(e).value;
                std::string s = t.str();
                if (s.find('.') == std::string::npos &&
                    s.find('e') == std::string::npos &&
                    s.find('n') == std::string::npos)
                    s += ".0";
                out << s;
                break;
            }
            case ExprKind::BoolLit:
                out << (static_cast<const BoolLit&>(e).value ? "true" : "false");
                break;
            case ExprKind::StringLit: {
                out << '"';
                for (char c : static_cast<const StringLit&>(e).value) {
                    switch (c) {
                        case '"': out << "\\\""; break;
                        case '\\': out << "\\\\"; break;
                        case '\n': out << "\\n"; break;
                        case '\t': out << "\\t"; break;
                        case '\r': out << "\\r"; break;
                        default: out << c;
                    }
                }
                out << '"';
                break;
            }
            case ExprKind::Var:
                out << static_cast<const Var&>(e).name;
                break;
            case ExprKind::Unary: {
                auto& u = static_cast<const Unary&>(e);
                out << "(" << u.op;
                writeExpr(*u.operand);
                out << ")";
                break;
            }
            case ExprKind::Binary: {
                auto& b = static_cast<const Binary&>(e);
                out << "(";
                writeExpr(*b.lhs);
                out << " " << b.op << " ";
                writeExpr(*b.rhs);
                out << ")";
                break;
            }
            case ExprKind::Call: {
                auto& c = static_cast<const Call&>(e);
                out << c.callee << "(";
                for (size_t i = 0; i < c.args.size(); i++) {
                    if (i) out << ", ";
                    writeExpr(*c.args[i]);
                }
                out << ")";
                break;
            }
            case ExprKind::Index: {
                auto& ix = static_cast<const IndexExpr&>(e);
                writeExpr(*ix.arr);
                out << "[";
                writeExpr(*ix.idx);
                out << "]";
                break;
            }
            case ExprKind::ArrayLit: {
                auto& al = static_cast<const ArrayLit&>(e);
                out << "[";
                for (size_t i = 0; i < al.elems.size(); i++) {
                    if (i) out << ", ";
                    writeExpr(*al.elems[i]);
                }
                out << "]";
                break;
            }
        }
    }

    void writeBlock(const Block& b) {
        out << "{\n";
        depth++;
        for (auto& s : b.stmts) writeStmt(*s);
        depth--;
        indent();
        out << "}";
    }

    void writeStmt(const Stmt& s) {
        indent();
        switch (s.kind) {
            case StmtKind::Let: {
                auto& ls = static_cast<const LetStmt&>(s);
                out << "let " << ls.name << ": " << typeName(ls.declared);
                if (ls.declaredLen > 0) out << "[" << ls.declaredLen << "]";
                if (ls.init) { out << " = "; writeExpr(*ls.init); }
                out << "\n";
                break;
            }
            case StmtKind::Assign: {
                auto& as = static_cast<const AssignStmt&>(s);
                out << as.name;
                if (as.index) { out << "["; writeExpr(*as.index); out << "]"; }
                out << " = ";
                writeExpr(*as.value);
                out << "\n";
                break;
            }
            case StmtKind::If: {
                auto& is = static_cast<const IfStmt&>(s);
                out << "if ";
                writeExpr(*is.cond);
                out << " ";
                writeBlock(*is.thenBlock);
                if (is.elseBlock) {
                    out << " else ";
                    writeBlock(*is.elseBlock);
                }
                out << "\n";
                break;
            }
            case StmtKind::While: {
                auto& ws = static_cast<const WhileStmt&>(s);
                out << "while ";
                writeExpr(*ws.cond);
                out << " ";
                writeBlock(*ws.body);
                out << "\n";
                break;
            }
            case StmtKind::Return: {
                auto& rs = static_cast<const ReturnStmt&>(s);
                out << "return";
                if (rs.value) { out << " "; writeExpr(*rs.value); }
                out << "\n";
                break;
            }
            case StmtKind::ExprStmt: {
                auto& es = static_cast<const ExprStmt&>(s);
                writeExpr(*es.expr);
                out << "\n";
                break;
            }
            case StmtKind::Block:
                writeBlock(static_cast<const Block&>(s));
                out << "\n";
                break;
        }
    }

    void writeFn(const FnDecl& fn) {
        if (fn.isExtern) out << "extern ";
        out << "fn " << fn.name << "(";
        for (size_t i = 0; i < fn.params.size(); i++) {
            if (i) out << ", ";
            out << fn.params[i].name << ": " << typeName(fn.params[i].type);
        }
        out << ")";
        if (fn.ret != Type::Void) out << " -> " << typeName(fn.ret);
        if (fn.isExtern) {
            out << "\n";
        } else {
            out << " ";
            writeBlock(*fn.body);
            out << "\n";
        }
    }
};

} // namespace

std::string serializeSource(const Program& prog) {
    SourceWriter w;
    for (size_t i = 0; i < prog.fns.size(); i++) {
        if (i) w.out << "\n";
        w.writeFn(*prog.fns[i]);
    }
    return w.out.str();
}

// ======================================================================
//  AST → 积木模型 JSON
// ======================================================================
namespace {

struct JsonWriter {
    std::ostringstream out;
    int depth = 0;

    void nl() { out << "\n"; for (int i = 0; i < depth; i++) out << "  "; }

    void str(const std::string& s) {
        out << '"';
        for (char c : s) {
            switch (c) {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\n': out << "\\n"; break;
                case '\t': out << "\\t"; break;
                default: out << c;
            }
        }
        out << '"';
    }

    // "key": value 形式的字段，由调用方控制逗号
    void key(const char* k) { str(k); out << ": "; }

    void expr(const Expr& e) {
        out << "{"; depth++;
        nl(); key("block");
        switch (e.kind) {
            case ExprKind::IntLit:
                str("int"); out << ","; nl(); key("value");
                out << static_cast<const IntLit&>(e).value;
                break;
            case ExprKind::FloatLit:
                str("float"); out << ","; nl(); key("value");
                out << static_cast<const FloatLit&>(e).value;
                break;
            case ExprKind::BoolLit:
                str("bool"); out << ","; nl(); key("value");
                out << (static_cast<const BoolLit&>(e).value ? "true" : "false");
                break;
            case ExprKind::StringLit:
                str("string"); out << ","; nl(); key("value");
                str(static_cast<const StringLit&>(e).value);
                break;
            case ExprKind::Var:
                str("var"); out << ","; nl(); key("name");
                str(static_cast<const Var&>(e).name);
                break;
            case ExprKind::Unary: {
                auto& u = static_cast<const Unary&>(e);
                str("unary"); out << ","; nl(); key("op"); str(u.op);
                out << ","; nl(); key("operand"); expr(*u.operand);
                break;
            }
            case ExprKind::Binary: {
                auto& b = static_cast<const Binary&>(e);
                str("binary"); out << ","; nl(); key("op"); str(b.op);
                out << ","; nl(); key("lhs"); expr(*b.lhs);
                out << ","; nl(); key("rhs"); expr(*b.rhs);
                break;
            }
            case ExprKind::Call: {
                auto& c = static_cast<const Call&>(e);
                str("call"); out << ","; nl(); key("callee"); str(c.callee);
                out << ","; nl(); key("args"); array(c.args);
                break;
            }
            case ExprKind::Index: {
                auto& ix = static_cast<const IndexExpr&>(e);
                str("index"); out << ","; nl(); key("arr"); expr(*ix.arr);
                out << ","; nl(); key("idx"); expr(*ix.idx);
                break;
            }
            case ExprKind::ArrayLit: {
                auto& al = static_cast<const ArrayLit&>(e);
                str("array"); out << ","; nl(); key("elems"); array(al.elems);
                break;
            }
        }
        depth--; nl(); out << "}";
    }

    template <typename Vec>
    void array(const Vec& items) {
        if (items.empty()) { out << "[]"; return; }
        out << "["; depth++;
        for (size_t i = 0; i < items.size(); i++) {
            if (i) out << ",";
            nl(); expr(*items[i]);
        }
        depth--; nl(); out << "]";
    }

    void stmtList(const std::vector<StmtPtr>& stmts) {
        if (stmts.empty()) { out << "[]"; return; }
        out << "["; depth++;
        for (size_t i = 0; i < stmts.size(); i++) {
            if (i) out << ",";
            nl(); stmt(*stmts[i]);
        }
        depth--; nl(); out << "]";
    }

    void stmt(const Stmt& s) {
        out << "{"; depth++;
        nl(); key("block");
        switch (s.kind) {
            case StmtKind::Let: {
                auto& ls = static_cast<const LetStmt&>(s);
                str("let"); out << ","; nl(); key("name"); str(ls.name);
                out << ","; nl(); key("type"); str(typeName(ls.declared));
                out << ","; nl(); key("len"); out << ls.declaredLen;
                if (ls.init) { out << ","; nl(); key("value"); expr(*ls.init); }
                break;
            }
            case StmtKind::Assign: {
                auto& as = static_cast<const AssignStmt&>(s);
                str("assign"); out << ","; nl(); key("name"); str(as.name);
                if (as.index) { out << ","; nl(); key("index"); expr(*as.index); }
                out << ","; nl(); key("value"); expr(*as.value);
                break;
            }
            case StmtKind::If: {
                auto& is = static_cast<const IfStmt&>(s);
                str("if"); out << ","; nl(); key("cond"); expr(*is.cond);
                out << ","; nl(); key("then"); stmtList(is.thenBlock->stmts);
                if (is.elseBlock) {
                    out << ","; nl(); key("else"); stmtList(is.elseBlock->stmts);
                }
                break;
            }
            case StmtKind::While: {
                auto& ws = static_cast<const WhileStmt&>(s);
                str("while"); out << ","; nl(); key("cond"); expr(*ws.cond);
                out << ","; nl(); key("body"); stmtList(ws.body->stmts);
                break;
            }
            case StmtKind::Return: {
                auto& rs = static_cast<const ReturnStmt&>(s);
                str("return");
                if (rs.value) { out << ","; nl(); key("value"); expr(*rs.value); }
                break;
            }
            case StmtKind::ExprStmt: {
                auto& es = static_cast<const ExprStmt&>(s);
                str("expr"); out << ","; nl(); key("expr"); expr(*es.expr);
                break;
            }
            case StmtKind::Block: {
                auto& b = static_cast<const Block&>(s);
                str("block_group"); out << ","; nl(); key("body");
                stmtList(b.stmts);
                break;
            }
        }
        depth--; nl(); out << "}";
    }

    void fn(const FnDecl& f) {
        out << "{"; depth++;
        nl(); key("block"); str(f.isExtern ? "extern_fn" : "fn");
        out << ","; nl(); key("name"); str(f.name);
        out << ","; nl(); key("params"); out << "[";
        if (!f.params.empty()) {
            depth++;
            for (size_t i = 0; i < f.params.size(); i++) {
                if (i) out << ",";
                nl(); out << "{"; depth++;
                nl(); key("name"); str(f.params[i].name);
                out << ","; nl(); key("type"); str(typeName(f.params[i].type));
                depth--; nl(); out << "}";
            }
            depth--; nl();
        }
        out << "]";
        out << ","; nl(); key("ret"); str(typeName(f.ret));
        if (!f.isExtern) {
            out << ","; nl(); key("body"); stmtList(f.body->stmts);
        }
        depth--; nl(); out << "}";
    }
};

} // namespace

std::string serializeBlocks(const Program& prog) {
    JsonWriter w;
    w.out << "{"; w.depth++;
    w.nl(); w.key("program"); w.out << "[";
    if (!prog.fns.empty()) {
        w.depth++;
        for (size_t i = 0; i < prog.fns.size(); i++) {
            if (i) w.out << ",";
            w.nl(); w.fn(*prog.fns[i]);
        }
        w.depth--; w.nl();
    }
    w.out << "]";
    w.depth--; w.nl(); w.out << "}\n";
    return w.out.str();
}

} // namespace sincoding
