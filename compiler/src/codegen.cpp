#include "codegen.h"

namespace sincoding {

void CodeGen::indent() {
    for (int i = 0; i < depth_; i++) out_ << "    ";
}

std::string CodeGen::generate(const Program& prog) {
    out_ << "// 由 Sincoding 编译器自动生成，请勿手改\n";
    out_ << "#include <stdio.h>\n";
    out_ << "#include <stdbool.h>\n\n";

    // 前向声明
    for (auto& fn : prog.fns) emitFnProto(*fn);
    out_ << "\n";

    // 函数体
    for (auto& fn : prog.fns) {
        emitFn(*fn);
        out_ << "\n";
    }
    return out_.str();
}

// main 在 C 里必须返回 int，否则触发 -Wmain；其余函数按类型映射。
static const char* fnRetC(const FnDecl& fn) {
    if (fn.name == "main" && fn.ret == Type::Int) return "int";
    return typeToC(fn.ret);
}

void CodeGen::emitFnProto(const FnDecl& fn) {
    out_ << fnRetC(fn) << " " << fn.name << "(";
    if (fn.params.empty()) {
        out_ << "void";
    } else {
        for (size_t i = 0; i < fn.params.size(); i++) {
            if (i) out_ << ", ";
            out_ << typeToC(fn.params[i].type) << " " << fn.params[i].name;
        }
    }
    out_ << ");\n";
}

void CodeGen::emitFn(const FnDecl& fn) {
    out_ << fnRetC(fn) << " " << fn.name << "(";
    if (fn.params.empty()) {
        out_ << "void";
    } else {
        for (size_t i = 0; i < fn.params.size(); i++) {
            if (i) out_ << ", ";
            out_ << typeToC(fn.params[i].type) << " " << fn.params[i].name;
        }
    }
    out_ << ") ";
    emitBlock(*fn.body);
}

void CodeGen::emitBlock(const Block& block) {
    out_ << "{\n";
    depth_++;
    for (auto& s : block.stmts) emitStmt(*s);
    depth_--;
    indent();
    out_ << "}\n";
}

void CodeGen::emitStmt(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Let: {
            auto& ls = static_cast<const LetStmt&>(s);
            indent();
            out_ << typeToC(ls.declared) << " " << ls.name << " = ";
            emitExpr(*ls.init);
            out_ << ";\n";
            break;
        }
        case StmtKind::Assign: {
            auto& as = static_cast<const AssignStmt&>(s);
            indent();
            out_ << as.name << " = ";
            emitExpr(*as.value);
            out_ << ";\n";
            break;
        }
        case StmtKind::If: {
            auto& is = static_cast<const IfStmt&>(s);
            indent();
            out_ << "if (";
            emitExpr(*is.cond);
            out_ << ") ";
            emitBlock(*is.thenBlock);
            if (is.elseBlock) {
                indent();
                out_ << "else ";
                emitBlock(*is.elseBlock);
            }
            break;
        }
        case StmtKind::While: {
            auto& ws = static_cast<const WhileStmt&>(s);
            indent();
            out_ << "while (";
            emitExpr(*ws.cond);
            out_ << ") ";
            emitBlock(*ws.body);
            break;
        }
        case StmtKind::Return: {
            auto& rs = static_cast<const ReturnStmt&>(s);
            indent();
            if (rs.value) {
                out_ << "return ";
                emitExpr(*rs.value);
                out_ << ";\n";
            } else {
                out_ << "return;\n";
            }
            break;
        }
        case StmtKind::ExprStmt: {
            auto& es = static_cast<const ExprStmt&>(s);
            indent();
            emitExpr(*es.expr);
            out_ << ";\n";
            break;
        }
        case StmtKind::Block:
            indent();
            emitBlock(static_cast<const Block&>(s));
            break;
    }
}

void CodeGen::emitPrint(const Call& c) {
    // 依据参数静态类型选择 printf 格式串
    const Expr& arg = *c.args[0];
    out_ << "printf(";
    switch (arg.type) {
        case Type::Int:   out_ << "\"%lld\\n\", (long long)("; break;
        case Type::Float: out_ << "\"%g\\n\", (double)("; break;
        case Type::Bool:  out_ << "\"%s\\n\", ("; break;
        default:          out_ << "\"%lld\\n\", (long long)("; break;
    }
    if (arg.type == Type::Bool) {
        emitExpr(arg);
        out_ << ") ? \"true\" : \"false\"";
    } else {
        emitExpr(arg);
        out_ << ")";
    }
    out_ << ")";
}

void CodeGen::emitExpr(const Expr& e) {
    switch (e.kind) {
        case ExprKind::IntLit:
            out_ << static_cast<const IntLit&>(e).value << "LL";
            break;
        case ExprKind::FloatLit: {
            // 确保输出带小数点，避免被当成整型
            std::ostringstream tmp;
            tmp << static_cast<const FloatLit&>(e).value;
            std::string s = tmp.str();
            if (s.find('.') == std::string::npos &&
                s.find('e') == std::string::npos &&
                s.find('n') == std::string::npos /*inf/nan*/)
                s += ".0";
            out_ << s;
            break;
        }
        case ExprKind::BoolLit:
            out_ << (static_cast<const BoolLit&>(e).value ? "true" : "false");
            break;
        case ExprKind::Var:
            out_ << static_cast<const Var&>(e).name;
            break;
        case ExprKind::Unary: {
            auto& u = static_cast<const Unary&>(e);
            out_ << "(" << u.op;
            emitExpr(*u.operand);
            out_ << ")";
            break;
        }
        case ExprKind::Binary: {
            auto& b = static_cast<const Binary&>(e);
            out_ << "(";
            emitExpr(*b.lhs);
            out_ << " " << b.op << " ";
            emitExpr(*b.rhs);
            out_ << ")";
            break;
        }
        case ExprKind::Call: {
            auto& c = static_cast<const Call&>(e);
            if (c.callee == "print") { emitPrint(c); break; }
            out_ << c.callee << "(";
            for (size_t i = 0; i < c.args.size(); i++) {
                if (i) out_ << ", ";
                emitExpr(*c.args[i]);
            }
            out_ << ")";
            break;
        }
    }
}

} // namespace sincoding
