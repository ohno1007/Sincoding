#include "codegen.h"
#include <cstdio>

namespace sincoding {

void CodeGen::indent() {
    for (int i = 0; i < depth_; i++) out_ << "    ";
}

std::string CodeGen::generate(const Program& prog) {
    out_ << "// 由 Sincoding 编译器自动生成，请勿手改\n";
    out_ << "#include <stdio.h>\n";
    out_ << "#include <stdbool.h>\n";
    out_ << "#include <string.h>\n\n";

    // 结构体类型定义
    if (!prog.structs.empty()) {
        for (auto& st : prog.structs) {
            out_ << "typedef struct {\n";
            for (auto& f : st->fields)
                out_ << "    " << typeToC(f.type) << " " << f.name << ";\n";
            out_ << "} " << st->name << ";\n";
        }
        out_ << "\n";
    }

    // 前向声明
    for (auto& fn : prog.fns) emitFnProto(*fn);
    out_ << "\n";

    // 全局变量（文件作用域）
    if (!prog.globals.empty()) {
        for (auto& g : prog.globals) emitStmt(*g);
        out_ << "\n";
    }

    // 函数体（extern 声明无函数体，仅靠上面的原型链接到运行时）
    for (auto& fn : prog.fns) {
        if (fn->isExtern) continue;
        emitFn(*fn);
        out_ << "\n";
    }
    return out_.str();
}

// 含结构体名的 C 类型
static std::string cType(Type t, const std::string& structName) {
    if (t == Type::Struct) return structName;
    return typeToC(t);
}

// main 在 C 里必须返回 int，否则触发 -Wmain；其余函数按类型映射。
static std::string fnRetC(const FnDecl& fn) {
    if (fn.name == "main" && fn.ret == Type::Int) return "int";
    if (fn.ret == Type::Struct) return fn.retStruct;
    return typeToC(fn.ret);
}

void CodeGen::emitFnProto(const FnDecl& fn) {
    if (fn.isExtern) out_ << "extern ";
    out_ << fnRetC(fn) << " " << fn.name << "(";
    if (fn.params.empty()) {
        out_ << "void";
    } else {
        for (size_t i = 0; i < fn.params.size(); i++) {
            if (i) out_ << ", ";
            out_ << cType(fn.params[i].type, fn.params[i].structName) << " " << fn.params[i].name;
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
            out_ << cType(fn.params[i].type, fn.params[i].structName) << " " << fn.params[i].name;
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
            out_ << cType(ls.declared, ls.structName) << " " << ls.name;
            if (ls.declaredLen > 0) out_ << "[" << ls.declaredLen << "]";
            out_ << " = ";
            if (ls.init) {
                emitExpr(*ls.init);
            } else if (ls.declaredLen > 0 || ls.declared == Type::Struct) {
                out_ << "{0}";                       // 数组 / 结构体零初始化
            } else {                                 // 标量默认值
                switch (ls.declared) {
                    case Type::Float: out_ << "0.0"; break;
                    case Type::Bool: out_ << "false"; break;
                    case Type::String: out_ << "\"\""; break;
                    default: out_ << "0"; break;
                }
            }
            out_ << ";\n";
            break;
        }
        case StmtKind::Assign: {
            auto& as = static_cast<const AssignStmt&>(s);
            indent();
            out_ << as.name;
            if (as.index) { out_ << "["; emitExpr(*as.index); out_ << "]"; }
            if (!as.field.empty()) out_ << "." << as.field;
            out_ << " = ";
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
        case StmtKind::For: {
            auto& fs = static_cast<const ForStmt&>(s);
            indent();
            out_ << "for (long long " << fs.var << " = ";
            emitExpr(*fs.start);
            out_ << "; " << fs.var << " < ";
            emitExpr(*fs.end);
            out_ << "; " << fs.var << "++) ";
            emitBlock(*fs.body);
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
        case Type::Int:    out_ << "\"%lld\\n\", (long long)("; break;
        case Type::Float:  out_ << "\"%g\\n\", (double)("; break;
        case Type::Bool:   out_ << "\"%s\\n\", ("; break;
        case Type::String: out_ << "\"%s\\n\", (const char*)("; break;
        default:           out_ << "\"%lld\\n\", (long long)("; break;
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
        case ExprKind::StringLit: {
            // 转义为 C 字符串字面量
            out_ << '"';
            for (unsigned char ch : static_cast<const StringLit&>(e).value) {
                switch (ch) {
                    case '"': out_ << "\\\""; break;
                    case '\\': out_ << "\\\\"; break;
                    case '\n': out_ << "\\n"; break;
                    case '\t': out_ << "\\t"; break;
                    case '\r': out_ << "\\r"; break;
                    default:
                        if (ch < 0x20) { // 其它控制字符用八进制
                            char buf[8]; std::snprintf(buf, sizeof(buf), "\\%03o", ch);
                            out_ << buf;
                        } else out_ << (char)ch;
                }
            }
            out_ << '"';
            break;
        }
        case ExprKind::Var:
            out_ << static_cast<const Var&>(e).name;
            break;
        case ExprKind::Index: {
            auto& ix = static_cast<const IndexExpr&>(e);
            emitExpr(*ix.arr);
            out_ << "[";
            emitExpr(*ix.idx);
            out_ << "]";
            break;
        }
        case ExprKind::ArrayLit: {
            auto& al = static_cast<const ArrayLit&>(e);
            out_ << "{";
            for (size_t i = 0; i < al.elems.size(); i++) {
                if (i) out_ << ", ";
                emitExpr(*al.elems[i]);
            }
            out_ << "}";
            break;
        }
        case ExprKind::Field: {
            auto& fa = static_cast<const FieldAccess&>(e);
            emitExpr(*fa.obj);
            out_ << "." << fa.field;
            break;
        }
        case ExprKind::StructLit: {
            auto& sl = static_cast<const StructLit&>(e);
            out_ << "(" << sl.typeName << "){";
            for (size_t i = 0; i < sl.fields.size(); i++) {
                if (i) out_ << ", ";
                out_ << "." << sl.fields[i].name << " = ";
                emitExpr(*sl.fields[i].value);
            }
            out_ << "}";
            break;
        }
        case ExprKind::Unary: {
            auto& u = static_cast<const Unary&>(e);
            out_ << "(" << u.op;
            emitExpr(*u.operand);
            out_ << ")";
            break;
        }
        case ExprKind::Binary: {
            auto& b = static_cast<const Binary&>(e);
            // 字符串相等比较走 strcmp
            if ((b.op == "==" || b.op == "!=") && b.lhs->type == Type::String) {
                out_ << "(strcmp(";
                emitExpr(*b.lhs);
                out_ << ", ";
                emitExpr(*b.rhs);
                out_ << ") " << b.op << " 0)";
                break;
            }
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
