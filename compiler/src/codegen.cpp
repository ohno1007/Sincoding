#include "codegen.h"
#include <cstdio>
#include <set>
#include <vector>

namespace sincoding {

// ---------- 数组值语义：C 侧用结构体包裹定长数组 ----------
// 语言的 T[N] 转成 `typedef struct { cT data[N]; } Arr_<tag>_<N>;`，
// 使数组像标量一样可赋值 / 传参 / 返回（统一值语义，无裸指针别名）。
namespace {

struct ArrType { Type elem; std::string structName; int len; };

std::string arrTag(Type t, const std::string& sn) {
    switch (t) {
        case Type::Int: return "int";
        case Type::Float: return "float";
        case Type::Bool: return "bool";
        case Type::String: return "str";
        case Type::Struct: return sn;
        default: return "x";
    }
}
std::string arrName(Type t, const std::string& sn, int len) {
    return "Arr_" + arrTag(t, sn) + "_" + std::to_string(len);
}
std::string arrElemC(Type t, const std::string& sn) {
    return t == Type::Struct ? sn : typeToC(t);
}

void addArr(Type t, const std::string& sn, int len,
            std::vector<ArrType>& out, std::set<std::string>& seen) {
    if (len <= 0) return;
    if (seen.insert(arrName(t, sn, len)).second) out.push_back({t, sn, len});
}
void collectStmt(const Stmt& s, std::vector<ArrType>& out, std::set<std::string>& seen);
void collectBlock(const Block& b, std::vector<ArrType>& out, std::set<std::string>& seen) {
    for (auto& s : b.stmts) collectStmt(*s, out, seen);
}
void collectStmt(const Stmt& s, std::vector<ArrType>& out, std::set<std::string>& seen) {
    switch (s.kind) {
        case StmtKind::Let: {
            auto& ls = static_cast<const LetStmt&>(s);
            addArr(ls.declared, ls.structName, ls.declaredLen, out, seen);
            break;
        }
        case StmtKind::If: {
            auto& is = static_cast<const IfStmt&>(s);
            collectBlock(*is.thenBlock, out, seen);
            if (is.elseBlock) collectBlock(*is.elseBlock, out, seen);
            break;
        }
        case StmtKind::While: collectBlock(*static_cast<const WhileStmt&>(s).body, out, seen); break;
        case StmtKind::For:   collectBlock(*static_cast<const ForStmt&>(s).body, out, seen); break;
        case StmtKind::Block: collectBlock(static_cast<const Block&>(s), out, seen); break;
        default: break;
    }
}

} // namespace

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

    // 数组包裹类型定义（值语义：可赋值 / 传参 / 返回）
    {
        std::vector<ArrType> arrs;
        std::set<std::string> seen;
        for (auto& g : prog.globals) collectStmt(*g, arrs, seen);
        for (auto& fn : prog.fns) {
            for (auto& p : fn->params) addArr(p.type, p.structName, p.len, arrs, seen);
            addArr(fn->ret, fn->retStruct, fn->retLen, arrs, seen);
            if (fn->body) collectBlock(*fn->body, arrs, seen);
        }
        if (!arrs.empty()) {
            for (auto& a : arrs)
                out_ << "typedef struct { " << arrElemC(a.elem, a.structName)
                     << " data[" << a.len << "]; } "
                     << arrName(a.elem, a.structName, a.len) << ";\n";
            out_ << "\n";
        }
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

// 参数的 C 类型（数组用包裹结构体，按值传递）
static std::string paramC(const Param& p) {
    if (p.len > 0) return arrName(p.type, p.structName, p.len);
    return cType(p.type, p.structName);
}

// main 在 C 里必须返回 int，否则触发 -Wmain；其余函数按类型映射。
static std::string fnRetC(const FnDecl& fn) {
    if (fn.name == "main" && fn.ret == Type::Int) return "int";
    if (fn.retLen > 0) return arrName(fn.ret, fn.retStruct, fn.retLen);
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
            out_ << paramC(fn.params[i]) << " " << fn.params[i].name;
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
            out_ << paramC(fn.params[i]) << " " << fn.params[i].name;
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
            if (ls.declaredLen > 0)
                out_ << arrName(ls.declared, ls.structName, ls.declaredLen) << " " << ls.name;
            else
                out_ << cType(ls.declared, ls.structName) << " " << ls.name;
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
            if (as.index) { out_ << ".data["; emitExpr(*as.index); out_ << "]"; }
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
            out_ << ".data[";
            emitExpr(*ix.idx);
            out_ << "]";
            break;
        }
        case ExprKind::ArrayLit: {
            auto& al = static_cast<const ArrayLit&>(e);
            // 复合字面量 (Arr_tag_N){{...}}：初始化与整体赋值位置都合法
            out_ << "(" << arrName(al.type, al.structName, al.arrayLen) << "){{";
            for (size_t i = 0; i < al.elems.size(); i++) {
                if (i) out_ << ", ";
                emitExpr(*al.elems[i]);
            }
            out_ << "}}";
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
