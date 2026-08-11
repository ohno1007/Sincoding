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
            case ExprKind::Field: {
                auto& fa = static_cast<const FieldAccess&>(e);
                writeExpr(*fa.obj);
                out << "." << fa.field;
                break;
            }
            case ExprKind::StructLit: {
                auto& sl = static_cast<const StructLit&>(e);
                out << sl.typeName << " { ";
                for (size_t i = 0; i < sl.fields.size(); i++) {
                    if (i) out << ", ";
                    out << sl.fields[i].name << ": ";
                    writeExpr(*sl.fields[i].value);
                }
                out << " }";
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
                out << "let " << ls.name;
                // 类型未知（泛型模板体内不做具体类型检查）时省略标注，靠初始化推断
                if (ls.declared != Type::Unknown) {
                    out << ": " << (ls.declared == Type::Struct ? ls.structName : typeName(ls.declared));
                    if (ls.declaredLen > 0) out << "[" << ls.declaredLen << "]";
                    else if (ls.declaredLen == -1) out << "[]";
                }
                if (ls.init) { out << " = "; writeExpr(*ls.init); }
                out << "\n";
                break;
            }
            case StmtKind::Assign: {
                auto& as = static_cast<const AssignStmt&>(s);
                out << as.name;
                if (as.index) { out << "["; writeExpr(*as.index); out << "]"; }
                if (!as.field.empty()) out << "." << as.field;
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
            case StmtKind::For: {
                auto& fs = static_cast<const ForStmt&>(s);
                out << "for " << fs.var << " in ";
                writeExpr(*fs.start);
                out << "..";
                writeExpr(*fs.end);
                out << " ";
                writeBlock(*fs.body);
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

    void writeStruct(const StructDecl& st) {
        out << "struct " << st.name << " {\n";
        for (size_t i = 0; i < st.fields.size(); i++) {
            const auto& f = st.fields[i];
            out << "    " << f.name << ": " << ptype(f.type, f.structName);
            if (f.len > 0) out << "[" << f.len << "]";
            else if (f.len == -1) out << "[]";
            if (i + 1 < st.fields.size()) out << ",";
            out << "\n";
        }
        out << "}\n";
    }

    static std::string ptype(Type t, const std::string& sn) {
        return t == Type::Struct ? sn : typeName(t);
    }

    void writeFn(const FnDecl& fn) {
        if (fn.isExtern) out << "extern ";
        out << "fn " << fn.name;
        if (!fn.typeParams.empty()) {                 // 泛型模板：写回 <T, U>
            out << "<";
            for (size_t i = 0; i < fn.typeParams.size(); i++) {
                if (i) out << ", ";
                out << fn.typeParams[i];
            }
            out << ">";
        }
        out << "(";
        for (size_t i = 0; i < fn.params.size(); i++) {
            if (i) out << ", ";
            out << fn.params[i].name << ": " << ptype(fn.params[i].type, fn.params[i].structName);
            if (fn.params[i].len > 0) out << "[" << fn.params[i].len << "]";
            else if (fn.params[i].len == -1) out << "[]";
        }
        out << ")";
        if (fn.ret != Type::Void) {
            out << " -> " << ptype(fn.ret, fn.retStruct);
            if (fn.retLen > 0) out << "[" << fn.retLen << "]";
            else if (fn.retLen == -1) out << "[]";
        }
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
    // 导入：只写回 import 行本身；被导入的声明（module 非空）一律跳过，
    // 否则库源码会被灌进用户文件，破坏「源码 → AST → 源码」的幂等。
    for (auto& im : prog.imports) w.out << "import \"" << im.name << "\"\n";
    if (!prog.imports.empty()) w.out << "\n";

    for (auto& st : prog.structs)
        if (st->module.empty()) { w.writeStruct(*st); w.out << "\n"; }   // 结构体在最前
    bool anyGlobal = false;
    for (auto& g : prog.globals)
        if (g->module.empty()) { w.writeStmt(*g); anyGlobal = true; }    // 全局变量
    if (anyGlobal) w.out << "\n";
    bool first = true;
    for (auto& fn : prog.fns) {
        if (!fn->module.empty()) continue;
        if (!first) w.out << "\n";
        first = false;
        w.writeFn(*fn);
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
            case ExprKind::Field: {
                auto& fa = static_cast<const FieldAccess&>(e);
                str("field"); out << ","; nl(); key("obj"); expr(*fa.obj);
                out << ","; nl(); key("name"); str(fa.field);
                break;
            }
            case ExprKind::StructLit: {
                auto& sl = static_cast<const StructLit&>(e);
                str("structlit"); out << ","; nl(); key("typeName"); str(sl.typeName);
                out << ","; nl(); key("fields");
                if (sl.fields.empty()) { out << "[]"; }
                else {
                    out << "["; depth++;
                    for (size_t i = 0; i < sl.fields.size(); i++) {
                        if (i) out << ",";
                        nl(); out << "{"; depth++;
                        nl(); key("name"); str(sl.fields[i].name);
                        out << ","; nl(); key("value"); expr(*sl.fields[i].value);
                        depth--; nl(); out << "}";
                    }
                    depth--; nl(); out << "]";
                }
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
                out << ","; nl(); key("type");
                str(ls.declared == Type::Struct ? ls.structName : typeName(ls.declared));
                out << ","; nl(); key("len"); out << ls.declaredLen;
                if (ls.init) { out << ","; nl(); key("value"); expr(*ls.init); }
                break;
            }
            case StmtKind::Assign: {
                auto& as = static_cast<const AssignStmt&>(s);
                str("assign"); out << ","; nl(); key("name"); str(as.name);
                if (as.index) { out << ","; nl(); key("index"); expr(*as.index); }
                if (!as.field.empty()) { out << ","; nl(); key("field"); str(as.field); }
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
            case StmtKind::For: {
                auto& fs = static_cast<const ForStmt&>(s);
                str("for"); out << ","; nl(); key("var"); str(fs.var);
                out << ","; nl(); key("start"); expr(*fs.start);
                out << ","; nl(); key("end"); expr(*fs.end);
                out << ","; nl(); key("body"); stmtList(fs.body->stmts);
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
        if (!f.typeParams.empty()) {                    // 泛型类型参数 <T, U>
            out << ","; nl(); key("typeParams"); out << "[";
            for (size_t i = 0; i < f.typeParams.size(); i++) {
                if (i) out << ", ";
                str(f.typeParams[i]);
            }
            out << "]";
        }
        out << ","; nl(); key("params"); out << "[";
        if (!f.params.empty()) {
            depth++;
            for (size_t i = 0; i < f.params.size(); i++) {
                if (i) out << ",";
                nl(); out << "{"; depth++;
                nl(); key("name"); str(f.params[i].name);
                out << ","; nl(); key("type");
                str(f.params[i].type == Type::Struct ? f.params[i].structName
                                                      : typeName(f.params[i].type));
                out << ","; nl(); key("len"); out << f.params[i].len;   // T[N] / T[]
                depth--; nl(); out << "}";
            }
            depth--; nl();
        }
        out << "]";
        out << ","; nl(); key("ret");
        str(f.ret == Type::Struct ? f.retStruct : typeName(f.ret));
        out << ","; nl(); key("retLen"); out << f.retLen;               // 返回 T[N]
        if (!f.isExtern) {
            out << ","; nl(); key("body"); stmtList(f.body->stmts);
        }
        depth--; nl(); out << "}";
    }
};

} // namespace

std::string serializeBlocks(const Program& prog) {
    JsonWriter w;
    // 只导出用户自己的声明；import 进来的（module 非空）不进积木画布，
    // 只在 "imports" 里列出模块名（供编辑器渲染库分类 / 只读展示）。
    std::vector<const StructDecl*> structs;
    std::vector<const Stmt*> globals;
    std::vector<const FnDecl*> fns;
    for (auto& st : prog.structs) if (st->module.empty()) structs.push_back(st.get());
    for (auto& g  : prog.globals) if (g->module.empty())  globals.push_back(g.get());
    for (auto& fn : prog.fns)     if (fn->module.empty()) fns.push_back(fn.get());

    w.out << "{"; w.depth++;
    // 导入的模块名
    w.nl(); w.key("imports");
    if (prog.imports.empty()) { w.out << "[]"; }
    else {
        w.out << "[";
        for (size_t i = 0; i < prog.imports.size(); i++) {
            if (i) w.out << ", ";
            w.str(prog.imports[i].name);
        }
        w.out << "]";
    }
    w.out << ",";
    // 被导入函数的签名 —— 编辑器据此**自动生成积木**（导入即得积木）。
    // 只给签名：参数名/类型决定槽位，返回类型决定是语句块还是 reporter。
    w.nl(); w.key("libs");
    {
        std::vector<const FnDecl*> libFns;
        for (auto& fn : prog.fns)
            if (!fn->module.empty() && !fn->isExtern) libFns.push_back(fn.get());
        if (libFns.empty()) { w.out << "[]"; }
        else {
            w.out << "["; w.depth++;
            for (size_t i = 0; i < libFns.size(); i++) {
                if (i) w.out << ",";
                const auto& f = *libFns[i];
                w.nl(); w.out << "{"; w.depth++;
                w.nl(); w.key("module"); w.str(f.module);
                w.out << ","; w.nl(); w.key("name"); w.str(f.name);
                w.out << ","; w.nl(); w.key("params"); w.out << "[";
                for (size_t j = 0; j < f.params.size(); j++) {
                    if (j) w.out << ", ";
                    w.out << "{"; w.key("name"); w.str(f.params[j].name);
                    w.out << ", "; w.key("type");
                    w.str(f.params[j].type == Type::Struct ? f.params[j].structName
                                                           : typeName(f.params[j].type));
                    w.out << ", "; w.key("len"); w.out << f.params[j].len;
                    w.out << "}";
                }
                w.out << "]";
                w.out << ","; w.nl(); w.key("ret");
                w.str(f.ret == Type::Struct ? f.retStruct : typeName(f.ret));
                w.depth--; w.nl(); w.out << "}";
            }
            w.depth--; w.nl(); w.out << "]";
        }
    }
    w.out << ",";
    // 结构体定义
    w.nl(); w.key("structs");
    if (structs.empty()) {
        w.out << "[]";
    } else {
        w.out << "["; w.depth++;
        for (size_t i = 0; i < structs.size(); i++) {
            if (i) w.out << ",";
            const auto& st = *structs[i];
            w.nl(); w.out << "{"; w.depth++;
            w.nl(); w.key("name"); w.str(st.name);
            w.out << ","; w.nl(); w.key("fields"); w.out << "[";
            if (!st.fields.empty()) {
                w.depth++;
                for (size_t j = 0; j < st.fields.size(); j++) {
                    if (j) w.out << ",";
                    w.nl(); w.out << "{"; w.depth++;
                    w.nl(); w.key("name"); w.str(st.fields[j].name);
                    w.out << ","; w.nl(); w.key("type");
                    w.str(st.fields[j].type == Type::Struct ? st.fields[j].structName
                                                            : typeName(st.fields[j].type));
                    if (st.fields[j].len > 0) { w.out << ","; w.nl(); w.key("len"); w.out << st.fields[j].len; }
                    w.depth--; w.nl(); w.out << "}";
                }
                w.depth--; w.nl();
            }
            w.out << "]";
            w.depth--; w.nl(); w.out << "}";
        }
        w.depth--; w.nl(); w.out << "]";
    }
    w.out << ",";
    // 全局变量
    w.nl(); w.key("globals");
    if (globals.empty()) {
        w.out << "[]";
    } else {
        w.out << "["; w.depth++;
        for (size_t i = 0; i < globals.size(); i++) {
            if (i) w.out << ",";
            w.nl(); w.stmt(*globals[i]);
        }
        w.depth--; w.nl(); w.out << "]";
    }
    w.out << ",";
    w.nl(); w.key("program"); w.out << "[";
    if (!fns.empty()) {
        w.depth++;
        for (size_t i = 0; i < fns.size(); i++) {
            if (i) w.out << ",";
            w.nl(); w.fn(*fns[i]);
        }
        w.depth--; w.nl();
    }
    w.out << "]";
    w.depth--; w.nl(); w.out << "}\n";
    return w.out.str();
}

} // namespace sincoding
