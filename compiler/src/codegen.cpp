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
// 切片 T[] 的 C 表示：{ 元素指针, 长度 } —— 借用视图，不拷贝数据
std::string sliceName(Type t, const std::string& sn) { return "Slice_" + arrTag(t, sn); }

void addArr(Type t, const std::string& sn, int len,
            std::vector<ArrType>& out, std::set<std::string>& seen) {
    if (len <= 0) return;                      // 0=非数组，-1=切片（另行收集）
    if (seen.insert(arrName(t, sn, len)).second) out.push_back({t, sn, len});
}
void addSlice(Type t, const std::string& sn, int len,
              std::vector<ArrType>& out, std::set<std::string>& seen) {
    if (len != -1) return;
    if (seen.insert(sliceName(t, sn)).second) out.push_back({t, sn, -1});
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

// ---------- 预扫描：程序是否用到字符串运行时（'+' 拼接 / str() 转换）----------
namespace {
bool exprUsesStrRt(const Expr& e);
template <typename Vec> bool anyUsesStrRt(const Vec& v) {
    for (auto& e : v) if (exprUsesStrRt(*e)) return true;
    return false;
}
bool exprUsesStrRt(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Binary: {
            auto& b = static_cast<const Binary&>(e);
            if (b.op == "+" && b.lhs->type == Type::String) return true;
            return exprUsesStrRt(*b.lhs) || exprUsesStrRt(*b.rhs);
        }
        case ExprKind::Call: {
            auto& c = static_cast<const Call&>(e);
            if (c.callee == "str") return true;
            return anyUsesStrRt(c.args);
        }
        case ExprKind::Unary:  return exprUsesStrRt(*static_cast<const Unary&>(e).operand);
        case ExprKind::Index: {
            auto& x = static_cast<const IndexExpr&>(e);
            return exprUsesStrRt(*x.arr) || exprUsesStrRt(*x.idx);
        }
        case ExprKind::ArrayLit: return anyUsesStrRt(static_cast<const ArrayLit&>(e).elems);
        case ExprKind::Field:    return exprUsesStrRt(*static_cast<const FieldAccess&>(e).obj);
        case ExprKind::StructLit:
            for (auto& fi : static_cast<const StructLit&>(e).fields)
                if (exprUsesStrRt(*fi.value)) return true;
            return false;
        default: return false;
    }
}
bool stmtUsesStrRt(const Stmt& s);
bool blockUsesStrRt(const Block& b) {
    for (auto& s : b.stmts) if (stmtUsesStrRt(*s)) return true;
    return false;
}
bool stmtUsesStrRt(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Let: { auto& l = static_cast<const LetStmt&>(s); return l.init && exprUsesStrRt(*l.init); }
        case StmtKind::Assign: {
            auto& a = static_cast<const AssignStmt&>(s);
            return (a.index && exprUsesStrRt(*a.index)) || exprUsesStrRt(*a.value);
        }
        case StmtKind::If: {
            auto& i = static_cast<const IfStmt&>(s);
            return exprUsesStrRt(*i.cond) || blockUsesStrRt(*i.thenBlock) ||
                   (i.elseBlock && blockUsesStrRt(*i.elseBlock));
        }
        case StmtKind::While: { auto& w = static_cast<const WhileStmt&>(s); return exprUsesStrRt(*w.cond) || blockUsesStrRt(*w.body); }
        case StmtKind::For: {
            auto& f = static_cast<const ForStmt&>(s);
            return exprUsesStrRt(*f.start) || exprUsesStrRt(*f.end) || blockUsesStrRt(*f.body);
        }
        case StmtKind::Return: { auto& r = static_cast<const ReturnStmt&>(s); return r.value && exprUsesStrRt(*r.value); }
        case StmtKind::ExprStmt: return exprUsesStrRt(*static_cast<const ExprStmt&>(s).expr);
        case StmtKind::Block: return blockUsesStrRt(static_cast<const Block&>(s));
    }
    return false;
}
bool progUsesStrRt(const Program& p) {
    for (auto& g : p.globals) if (stmtUsesStrRt(*g)) return true;
    for (auto& fn : p.fns) if (fn->body && blockUsesStrRt(*fn->body)) return true;
    return false;
}
} // namespace

void CodeGen::indent() {
    for (int i = 0; i < depth_; i++) out_ << "    ";
}

static std::string fieldC(const StructField& f);  // 定义在下方

std::string CodeGen::generate(const Program& prog) {
    out_ << "// 由 Sincoding 编译器自动生成，请勿手改\n";
    out_ << "#include <stdio.h>\n";
    out_ << "#include <stdbool.h>\n";
    out_ << "#include <string.h>\n\n";
    if (debug_) {                        // 调试钩子由 runtime 的 debug 模块实现
        out_ << "// --debug：调试钩子（F12 面板用）；发布构建不生成这些调用\n"
                "extern void sin_dbg_line(long long line);\n"
                "extern void sin_dbg_set_i(const char* n, long long v);\n"
                "extern void sin_dbg_set_f(const char* n, double v);\n"
                "extern void sin_dbg_set_b(const char* n, bool v);\n"
                "extern void sin_dbg_set_s(const char* n, const char* v);\n\n";
    }

    // 字符串运行时（仅在用到 '+' 拼接 / str() 时注入）：环形 arena，结果活到 arena 绕回
    if (progUsesStrRt(prog)) {
        out_ <<
"static char sin_str_arena[65536];\n"
"static size_t sin_str_pos = 0;\n"
"static const char* sin_str_take(const char* a, size_t la, const char* b, size_t lb) {\n"
"    if (sin_str_pos + la + lb + 1 > sizeof(sin_str_arena)) sin_str_pos = 0;\n"
"    char* p = sin_str_arena + sin_str_pos;\n"
"    if (la) memcpy(p, a, la);\n"
"    if (lb) memcpy(p + la, b, lb);\n"
"    p[la + lb] = 0;\n"
"    sin_str_pos += la + lb + 1;\n"
"    return p;\n"
"}\n"
"static const char* sin_str_concat(const char* a, const char* b) {\n"
"    return sin_str_take(a, strlen(a), b, strlen(b));\n"
"}\n"
"static const char* sin_str_from_int(long long n) {\n"
"    char buf[32]; int k = snprintf(buf, sizeof(buf), \"%lld\", n);\n"
"    return sin_str_take(buf, (size_t)k, \"\", 0);\n"
"}\n"
"static const char* sin_str_from_float(double f) {\n"
"    char buf[40]; int k = snprintf(buf, sizeof(buf), \"%g\", f);\n"
"    return sin_str_take(buf, (size_t)k, \"\", 0);\n"
"}\n"
"static const char* sin_str_from_bool(int b) { return b ? \"true\" : \"false\"; }\n\n";
    }

    // 泛型模板只是模板（含类型变量），不参与代码生成——只生成它的具体实例
    auto isTemplate = [](const FnDecl& f) { return !f.typeParams.empty(); };
    for (auto& fn : prog.fns) if (!isTemplate(*fn)) fns_[fn->name] = fn.get();  // 调用点查形参类型

    // 收集所有数组包裹类型（值语义）与切片类型（借用视图）
    std::vector<ArrType> arrs, slices;
    {
        std::set<std::string> seen, sseen;
        for (auto& fn : prog.fns) {
            if (isTemplate(*fn)) continue;
            for (auto& p : fn->params) addSlice(p.type, p.structName, p.len, slices, sseen);
            if (fn->body) {
                // 局部切片变量（如 let s = 某切片参数）
                std::vector<ArrType> tmp; std::set<std::string> tseen;
                collectBlock(*fn->body, tmp, tseen);
            }
        }
        for (auto& st : prog.structs)
            for (auto& f : st->fields) addArr(f.type, f.structName, f.len, arrs, seen);
        for (auto& g : prog.globals) collectStmt(*g, arrs, seen);
        for (auto& fn : prog.fns) {
            if (isTemplate(*fn)) continue;
            for (auto& p : fn->params) addArr(p.type, p.structName, p.len, arrs, seen);
            addArr(fn->ret, fn->retStruct, fn->retLen, arrs, seen);
            if (fn->body) collectBlock(*fn->body, arrs, seen);
        }
    }
    // 依赖顺序：标量元素数组 → 结构体 → 结构体元素数组
    auto emitArrs = [&](bool structElem) {
        bool any = false;
        for (auto& a : arrs) {
            if ((a.elem == Type::Struct) != structElem) continue;
            out_ << "typedef struct { " << arrElemC(a.elem, a.structName)
                 << " data[" << a.len << "]; } "
                 << arrName(a.elem, a.structName, a.len) << ";\n";
            any = true;
        }
        if (any) out_ << "\n";
    };

    emitArrs(/*structElem=*/false);          // 1) 标量数组（结构体字段可能依赖）
    if (!prog.structs.empty()) {             // 2) 结构体（字段可为标量数组 / 已声明结构体）
        for (auto& st : prog.structs) {
            out_ << "typedef struct {\n";
            for (auto& f : st->fields)
                out_ << "    " << fieldC(f) << " " << f.name << ";\n";
            out_ << "} " << st->name << ";\n";
        }
        out_ << "\n";
    }
    emitArrs(/*structElem=*/true);           // 3) 结构体数组（依赖结构体定义）
    if (!slices.empty()) {                   // 4) 切片视图（元素类型此时都已声明）
        for (auto& sl : slices)
            out_ << "typedef struct { " << arrElemC(sl.elem, sl.structName)
                 << "* data; long long len; } " << sliceName(sl.elem, sl.structName) << ";\n";
        out_ << "\n";
    }

    // 前向声明
    for (auto& fn : prog.fns) if (!isTemplate(*fn)) emitFnProto(*fn);
    out_ << "\n";

    // 全局变量（文件作用域）
    if (!prog.globals.empty()) {
        for (auto& g : prog.globals) emitStmt(*g);
        out_ << "\n";
    }

    // 函数体（extern 声明无函数体，仅靠上面的原型链接到运行时）
    for (auto& fn : prog.fns) {
        if (fn->isExtern || isTemplate(*fn)) continue;
        emitFn(*fn);
        out_ << "\n";
    }

    // 事件驱动：没写 main 但定义了事件函数（on_start / on_frame / on_key_*）时，
    // 合成主循环驱动。舞台隐式初始化——零样板，打开就能写游戏逻辑。
    // 顺序与编辑器预览解释器完全一致（预览 = 成品）。
    bool hasMain = false, hasEvent = false;
    auto has = [&](const char* n) {
        for (auto& fn : prog.fns) if (!fn->isExtern && fn->name == n) return true;
        return false;
    };
    for (auto& fn : prog.fns) {
        if (fn->isExtern) continue;
        if (fn->name == "main") hasMain = true;
        if (fn->name.rfind("on_", 0) == 0) hasEvent = true;
    }
    if (!hasMain && hasEvent) {
        out_ << "// 事件驱动主循环（编译器合成：用户程序只写事件函数）\n";
        out_ << "extern void stage_init(long long w, long long h);\n"
                "extern bool stage_running(void);\n"
                "extern void frame_begin(void);\n"
                "extern void frame_end(void);\n"
                "extern void stage_close(void);\n";
        static const struct { const char* fn; const char* pressed; } KEYS[] = {
            {"on_key_space", "key_pressed_space"}, {"on_key_left", "key_pressed_left"},
            {"on_key_right", "key_pressed_right"}, {"on_key_up", "key_pressed_up"},
            {"on_key_down", "key_pressed_down"},   {"on_click", "mouse_clicked"},
        };
        for (auto& k : KEYS)
            if (has(k.fn)) out_ << "extern bool " << k.pressed << "(void);\n";
        out_ << "int main(void) {\n"
                "    stage_init(480, 360);\n";
        if (has("on_start")) out_ << "    on_start();\n";
        out_ << "    while (stage_running()) {\n"
                "        frame_begin();\n";
        for (auto& k : KEYS)
            if (has(k.fn)) out_ << "        if (" << k.pressed << "()) " << k.fn << "();\n";
        if (has("on_frame")) out_ << "        on_frame();\n";
        out_ << "        frame_end();\n"
                "    }\n"
                "    stage_close();\n"
                "    return 0;\n"
                "}\n";
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
    if (p.len == -1) return sliceName(p.type, p.structName);   // 切片：借用视图
    if (p.len > 0) return arrName(p.type, p.structName, p.len);
    return cType(p.type, p.structName);
}

// 结构体字段的 C 类型（标量数组字段用包裹结构体，结构体字段用其名）
static std::string fieldC(const StructField& f) {
    if (f.len > 0) return arrName(f.type, f.structName, f.len);
    return cType(f.type, f.structName);
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

// 调试钩子：上报当前执行行 + 标量变量的值（仅 --debug 构建）
void CodeGen::emitDbgLine(const Stmt& s) {
    if (!debug_ || s.line <= 0) return;
    indent();
    out_ << "sin_dbg_line(" << s.line << ");\n";
}
void CodeGen::emitDbgVar(const std::string& name, Type t, int len) {
    if (!debug_ || len != 0) return;             // 仅监视标量
    const char* fn = nullptr;
    switch (t) {
        case Type::Int:    fn = "sin_dbg_set_i"; break;
        case Type::Float:  fn = "sin_dbg_set_f"; break;
        case Type::Bool:   fn = "sin_dbg_set_b"; break;
        case Type::String: fn = "sin_dbg_set_s"; break;
        default: return;
    }
    indent();
    out_ << fn << "(\"" << name << "\", " << name << ");\n";
}

void CodeGen::emitStmt(const Stmt& s) {
    emitDbgLine(s);
    switch (s.kind) {
        case StmtKind::Let: {
            auto& ls = static_cast<const LetStmt&>(s);
            indent();
            varTypes_[ls.name] = { ls.declared, ls.declaredLen };
            if (ls.declaredLen == -1)
                out_ << sliceName(ls.declared, ls.structName) << " " << ls.name;
            else if (ls.declaredLen > 0)
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
            emitDbgVar(ls.name, ls.declared, ls.declaredLen);
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
            if (!as.index && as.field.empty()) {      // 整变量赋值才好上报
                auto it = varTypes_.find(as.name);
                if (it != varTypes_.end()) emitDbgVar(as.name, it->second.first, it->second.second);
            }
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

void CodeGen::emitStr(const Call& c) {
    const Expr& a = *c.args[0];
    switch (a.type) {
        case Type::Int:    out_ << "sin_str_from_int(";   emitExpr(a); out_ << ")"; break;
        case Type::Float:  out_ << "sin_str_from_float("; emitExpr(a); out_ << ")"; break;
        case Type::Bool:   out_ << "sin_str_from_bool(";  emitExpr(a); out_ << ")"; break;
        default:           emitExpr(a); break;   // string：已是字符串
    }
}

// 实参：定长数组传给切片形参时，自动构造借用视图 { 数据指针, 长度 }
void CodeGen::emitArg(const Expr& a, const Param& p) {
    if (p.len == -1 && a.arrayLen > 0) {
        out_ << "(" << sliceName(p.type, p.structName) << "){ ";
        emitExpr(a);
        out_ << ".data, " << a.arrayLen << " }";
        return;
    }
    emitExpr(a);
}

// len(x)：切片取运行时长度，定长数组编译期即知
void CodeGen::emitLen(const Call& c) {
    const Expr& a = *c.args[0];
    if (a.arrayLen == -1) { out_ << "("; emitExpr(a); out_ << ").len"; }
    else out_ << a.arrayLen << "LL";
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
            if (b.lhs->type == Type::String) {
                if (b.op == "+") {                 // 拼接
                    out_ << "sin_str_concat(";
                    emitExpr(*b.lhs); out_ << ", "; emitExpr(*b.rhs);
                    out_ << ")";
                    break;
                }
                // 所有字符串比较（== != < <= > >=）走 strcmp 符号
                out_ << "(strcmp(";
                emitExpr(*b.lhs); out_ << ", "; emitExpr(*b.rhs);
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
            if (c.callee == "str") { emitStr(c); break; }
            if (c.callee == "len") { emitLen(c); break; }
            // 泛型调用发射单态化后的实例名；普通调用就是原名
            const std::string& target = c.resolved.empty() ? c.callee : c.resolved;
            auto fit = fns_.find(target);
            const FnDecl* callee = (fit == fns_.end()) ? nullptr : fit->second;
            out_ << target << "(";
            for (size_t i = 0; i < c.args.size(); i++) {
                if (i) out_ << ", ";
                if (callee && i < callee->params.size()) emitArg(*c.args[i], callee->params[i]);
                else emitExpr(*c.args[i]);
            }
            out_ << ")";
            break;
        }
    }
}

} // namespace sincoding
