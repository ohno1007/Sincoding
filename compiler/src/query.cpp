// query.cpp — IDE 查询实现（详见 query.h）
//
// 统一模型：collectOccurrences 收集全部标识符出现（定义 + 引用），每个出现带
// 位置、名字、种类（'v'ar 'f'n 's'truct 'F'ield）、所在函数、类型描述。
// hover / references / rename 都基于这一份出现表。
#include "query.h"
#include "serializer.h"
#include <algorithm>
#include <cctype>
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

// (type,len,structName) → 可读类型（int / int[5] / Point / Point[3]）
std::string typeStr(Type t, int len, const std::string& sn) {
    std::string base = (t == Type::Struct) ? (sn.empty() ? "struct" : sn) : typeName(t);
    if (len > 0) base += "[" + std::to_string(len) + "]";
    else if (len == -1) base += "[]";        // 切片
    else if (len == -2) base += "[*]";       // 动态列表
    return base;
}
std::string exprType(const Expr& e) { return typeStr(e.type, e.arrayLen, e.structName); }

const char* catName(char c) {
    switch (c) { case 'v': return "变量"; case 'f': return "函数"; case 's': return "结构体"; case 'F': return "字段"; default: return ""; }
}

// 一个标识符出现（定义或引用）
struct Occ { int line, col, len; std::string name; char cat; int fnIndex; std::string typ; };

void addOcc(std::vector<Occ>& out, int line, int col, const std::string& name, char cat, int fn, const std::string& typ) {
    if (!name.empty() && col > 0) out.push_back({line, col, (int)name.size(), name, cat, fn, typ});
}

void occExpr(const Expr& e, int fn, std::vector<Occ>& out) {
    switch (e.kind) {
        case ExprKind::Var: { auto& v = static_cast<const Var&>(e); addOcc(out, v.line, v.col, v.name, 'v', fn, exprType(v)); break; }
        case ExprKind::Call: {
            auto& c = static_cast<const Call&>(e);
            addOcc(out, c.line, c.col, c.callee, 'f', fn, exprType(c));
            for (auto& a : c.args) occExpr(*a, fn, out);
            break;
        }
        case ExprKind::Field: {
            auto& fa = static_cast<const FieldAccess&>(e);
            occExpr(*fa.obj, fn, out);
            addOcc(out, fa.line, fa.col, fa.field, 'F', fn, exprType(fa));
            break;
        }
        case ExprKind::StructLit: {
            auto& sl = static_cast<const StructLit&>(e);
            addOcc(out, sl.line, sl.col, sl.typeName, 's', fn, sl.typeName);
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
        case StmtKind::Let: { auto& l = static_cast<const LetStmt&>(s); addOcc(out, l.line, l.col, l.name, 'v', fn, typeStr(l.declared, l.declaredLen, l.structName)); if (l.init) occExpr(*l.init, fn, out); break; }
        case StmtKind::Assign: { auto& a = static_cast<const AssignStmt&>(s); addOcc(out, a.line, a.col, a.name, 'v', fn, ""); if (a.index) occExpr(*a.index, fn, out); occExpr(*a.value, fn, out); break; }
        case StmtKind::If: { auto& i = static_cast<const IfStmt&>(s); occExpr(*i.cond, fn, out); occBlock(*i.thenBlock, fn, out); if (i.elseBlock) occBlock(*i.elseBlock, fn, out); break; }
        case StmtKind::While: { auto& w = static_cast<const WhileStmt&>(s); occExpr(*w.cond, fn, out); occBlock(*w.body, fn, out); break; }
        case StmtKind::For: { auto& f = static_cast<const ForStmt&>(s); addOcc(out, f.line, f.col, f.var, 'v', fn, "int"); occExpr(*f.start, fn, out); occExpr(*f.end, fn, out); occBlock(*f.body, fn, out); break; }
        case StmtKind::Return: { auto& r = static_cast<const ReturnStmt&>(s); if (r.value) occExpr(*r.value, fn, out); break; }
        case StmtKind::ExprStmt: occExpr(*static_cast<const ExprStmt&>(s).expr, fn, out); break;
        case StmtKind::Block: occBlock(static_cast<const Block&>(s), fn, out); break;
        case StmtKind::Break: case StmtKind::Continue: break;
    }
}
std::vector<Occ> collectOccurrences(const Program& prog) {
    std::vector<Occ> out;
    for (auto& st : prog.structs) {
        addOcc(out, st->line, st->col, st->name, 's', -1, st->name);
        for (auto& f : st->fields) addOcc(out, f.line, f.col, f.name, 'F', -1, typeStr(f.type, f.len, f.structName));
    }
    for (size_t i = 0; i < prog.fns.size(); i++) {
        auto& fn = *prog.fns[i];
        addOcc(out, fn.line, fn.col, fn.name, 'f', -1, typeStr(fn.ret, fn.retLen, fn.retStruct));
        for (auto& p : fn.params) addOcc(out, p.line, p.col, p.name, 'v', (int)i, typeStr(p.type, p.len, p.structName));
        if (fn.body) occBlock(*fn.body, (int)i, out);
    }
    for (auto& g : prog.globals) occStmt(*g, -1, out);
    return out;
}
const Occ* occAt(const std::vector<Occ>& occs, int ql, int qc) {
    for (auto& o : occs) if (o.line == ql && o.col <= qc && qc < o.col + o.len) return &o;
    return nullptr;
}
// 变量作用域：局部（fnIndex>=0）限同函数；全局（-1）宽松匹配同名（近似）
bool sameVarScope(const Occ& a, const Occ& b) { return a.fnIndex < 0 ? true : a.fnIndex == b.fnIndex; }

// ---- 重命名：直接改 AST 名字字段，再由 serializeSource 输出 ----
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
        case StmtKind::Break: case StmtKind::Continue: break;
    }
}
void renameProgram(Program& prog, char cat, const std::string& from, const std::string& to, int fnIndex) {
    if (cat == 's') {
        for (auto& st : prog.structs) { if (st->name == from) st->name = to; }
        for (auto& fn : prog.fns) {
            if (fn->retStruct == from) fn->retStruct = to;
            for (auto& p : fn->params) if (p.structName == from) p.structName = to;
            if (fn->body) renBlock(*fn->body, cat, from, to);
        }
        for (auto& g : prog.globals) renStmt(*g, cat, from, to);
    } else if (cat == 'F') {
        for (auto& st : prog.structs) for (auto& f : st->fields) if (f.name == from) f.name = to;
        for (auto& fn : prog.fns) if (fn->body) renBlock(*fn->body, cat, from, to);
        for (auto& g : prog.globals) renStmt(*g, cat, from, to);
    } else if (cat == 'f') {
        for (auto& fn : prog.fns) { if (fn->name == from) fn->name = to; if (fn->body) renBlock(*fn->body, cat, from, to); }
        for (auto& g : prog.globals) renStmt(*g, cat, from, to);
    } else { // 'v'
        if (fnIndex >= 0 && fnIndex < (int)prog.fns.size()) {
            auto& fn = *prog.fns[fnIndex];
            for (auto& p : fn.params) if (p.name == from) p.name = to;
            if (fn.body) renBlock(*fn.body, cat, from, to);
        } else {
            for (auto& g : prog.globals) renStmt(*g, cat, from, to);
            for (auto& fn : prog.fns) if (fn->body) renBlock(*fn->body, cat, from, to);
        }
    }
}

} // namespace

std::string queryHover(const Program& prog, int line, int col) {
    auto occs = collectOccurrences(prog);
    const Occ* hit = occAt(occs, line, col);
    if (!hit) return "{\"found\":false}";
    return "{\"found\":true,\"name\":" + jesc(hit->name) +
           ",\"kind\":" + jesc(catName(hit->cat)) +
           ",\"type\":" + jesc(hit->typ) + "}";
}

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
        refs += "{\"line\":" + std::to_string(o.line) + ",\"col\":" + std::to_string(o.col) + ",\"len\":" + std::to_string(o.len) + "}";
    }
    refs += "]";
    return "{\"found\":true,\"name\":" + jesc(hit->name) + ",\"kind\":" + jesc(catName(hit->cat)) + ",\"refs\":" + refs + "}";
}

std::string applyRename(Program& prog, int line, int col, const std::string& newName) {
    auto occs = collectOccurrences(prog);
    const Occ* hit = occAt(occs, line, col);
    if (!hit) return "{\"ok\":false,\"note\":\"光标处不是可改名的标识符\"}";
    char cat = hit->cat;
    std::string from = hit->name;
    int fnIndex = hit->fnIndex;
    const char* note = (cat == 'v' && fnIndex < 0) ? "全局变量：跨函数按同名近似替换，请检查"
                     : (cat == 'F') ? "字段：按同名近似替换（可能涉及多个结构体），请检查" : "";
    renameProgram(prog, cat, from, newName, fnIndex);
    return "{\"ok\":true,\"source\":" + jesc(serializeSource(prog)) + ",\"note\":" + jesc(note) + "}";
}

// ================= 代码补全 =================
namespace {

struct Cand { std::string text, kind, detail; };
struct ScopeVar { std::string name, typ; };

// 源码第 line 行（1-based）中 col 之前的文本
std::string lineHead(const std::string& src, int line, int col) {
    int cur = 1;
    size_t i = 0;
    for (; i < src.size() && cur < line; i++) if (src[i] == '\n') cur++;
    size_t eol = src.find('\n', i);
    std::string ln = src.substr(i, (eol == std::string::npos ? src.size() : eol) - i);
    int take = col - 1;                       // col 是 1-based，光标在该列之前
    if (take < 0) take = 0;
    if (take > (int)ln.size()) take = (int)ln.size();
    return ln.substr(0, take);
}

bool identChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

// 从字符串尾部摘出标识符（返回它并从 s 尾部删掉）
std::string takeTrailingIdent(std::string& s) {
    size_t e = s.size();
    while (e > 0 && identChar(s[e - 1])) e--;
    std::string id = s.substr(e);
    if (!id.empty() && isdigit((unsigned char)id[0])) return "";   // 数字不是标识符
    s.resize(e);
    return id;
}

// 函数签名的可读形式：<T>(a: int, xs: int[]) -> int
std::string fnSig(const FnDecl& f) {
    std::string s;
    if (!f.typeParams.empty()) {
        s += "<";
        for (size_t i = 0; i < f.typeParams.size(); i++) { if (i) s += ", "; s += f.typeParams[i]; }
        s += ">";
    }
    s += "(";
    for (size_t i = 0; i < f.params.size(); i++) {
        if (i) s += ", ";
        s += f.params[i].name + ": " + typeStr(f.params[i].type, f.params[i].len, f.params[i].structName);
    }
    s += ")";
    if (f.ret != Type::Void) s += " -> " + typeStr(f.ret, f.retLen, f.retStruct);
    return s;
}

// 收集块内、光标行之前声明的变量（近似：不区分嵌套块的生存期，只按行号截断）
void collectVars(const Block& b, int upto, std::vector<ScopeVar>& out) {
    for (auto& sp : b.stmts) {
        const Stmt& s = *sp;
        if (s.line > upto) continue;
        switch (s.kind) {
            case StmtKind::Let: {
                auto& l = static_cast<const LetStmt&>(s);
                Type t = l.declared != Type::Unknown ? l.declared : (l.init ? l.init->type : Type::Unknown);
                int len = l.declaredLen ? l.declaredLen : (l.init ? l.init->arrayLen : 0);
                std::string sn = !l.structName.empty() ? l.structName : (l.init ? l.init->structName : "");
                out.push_back({l.name, typeStr(t, len, sn)});
                break;
            }
            case StmtKind::For: {
                auto& f = static_cast<const ForStmt&>(s);
                out.push_back({f.var, "int"});
                collectVars(*f.body, upto, out);
                break;
            }
            case StmtKind::If: {
                auto& i = static_cast<const IfStmt&>(s);
                collectVars(*i.thenBlock, upto, out);
                if (i.elseBlock) collectVars(*i.elseBlock, upto, out);
                break;
            }
            case StmtKind::While: collectVars(*static_cast<const WhileStmt&>(s).body, upto, out); break;
            case StmtKind::Block: collectVars(static_cast<const Block&>(s), upto, out); break;
            default: break;
        }
    }
}

// 单态化产生的实例（total__int）不是用户写过的名字，不该出现在补全里
bool isGenericInstance(const Program& prog, const FnDecl& f) {
    if (f.module == "<generic>") return true;
    size_t p = f.name.find("__");
    if (p == std::string::npos) return false;
    std::string base = f.name.substr(0, p);
    for (auto& g : prog.fns) if (!g->typeParams.empty() && g->name == base) return true;
    return false;
}

// 光标所在的用户函数下标（-1 = 顶层/全局区）
int enclosingFn(const Program& prog, int line) {
    int best = -1, bestLine = 0;
    for (size_t i = 0; i < prog.fns.size(); i++) {
        auto& f = *prog.fns[i];
        if (!f.module.empty() || !f.body) continue;   // 导入/外部函数不含光标
        if (f.line <= line && f.line >= bestLine) { bestLine = f.line; best = (int)i; }
    }
    return best;
}

const char* KEYWORDS[] = {"let", "fn", "if", "else", "while", "for", "in", "return",
                          "extern", "struct", "import", "true", "false"};
const char* PRIMS[] = {"int", "float", "bool", "string", "void"};

std::string candsJson(const std::string& ctx, const std::string& prefix, std::vector<Cand>& cands) {
    // 前缀过滤（不分大小写）+ 排序：短名优先，同长按字典序
    std::string pl;
    for (char c : prefix) pl += (char)tolower((unsigned char)c);
    std::vector<Cand> hit;
    for (auto& c : cands) {
        if (c.text == prefix) continue;                 // 已经打全了，不必提示
        std::string tl;
        for (char ch : c.text) tl += (char)tolower((unsigned char)ch);
        if (tl.compare(0, pl.size(), pl) != 0) continue;
        bool dup = false;
        for (auto& h : hit) if (h.text == c.text) { dup = true; break; }
        if (!dup) hit.push_back(c);
    }
    std::stable_sort(hit.begin(), hit.end(), [](const Cand& a, const Cand& b) {
        if (a.text.size() != b.text.size()) return a.text.size() < b.text.size();
        return a.text < b.text;
    });
    if (hit.size() > 20) hit.resize(20);
    std::string o = "{\"ctx\":" + jesc(ctx) + ",\"prefix\":" + jesc(prefix) + ",\"items\":[";
    for (size_t i = 0; i < hit.size(); i++) {
        if (i) o += ",";
        o += "{\"text\":" + jesc(hit[i].text) + ",\"kind\":" + jesc(hit[i].kind) +
             ",\"detail\":" + jesc(hit[i].detail) + "}";
    }
    return o + "]}";
}

} // namespace

std::string queryComplete(const Program& prog, const std::string& src, int line, int col) {
    std::string head = lineHead(src, line, col);
    std::string prefix = takeTrailingIdent(head);       // head 现在是前缀之前的部分
    std::vector<Cand> cands;

    // ---- 语境 1：成员补全（obj. / arr[i].）→ 结构体字段 ----
    if (!head.empty() && head.back() == '.') {
        std::string rest = head.substr(0, head.size() - 1);
        bool indexed = false;
        if (!rest.empty() && rest.back() == ']') {      // 跳过 [...]，取被索引的名字
            int depth = 0; size_t j = rest.size();
            while (j > 0) { char c = rest[--j]; if (c == ']') depth++; else if (c == '[') { if (--depth == 0) break; } }
            rest.resize(j);
            indexed = true;
        }
        std::string base = takeTrailingIdent(rest);
        std::string sname;
        if (!base.empty()) {
            int fi = enclosingFn(prog, line);
            for (auto& o : collectOccurrences(prog)) {
                if (o.cat != 'v' || o.name != base) continue;
                if (o.fnIndex >= 0 && o.fnIndex != fi) continue;
                std::string t = o.typ;
                size_t br = t.find('[');
                if (br != std::string::npos) { if (!indexed) continue; t = t.substr(0, br); } // 数组只有下标后才是元素
                else if (indexed) continue;
                sname = t;
                break;
            }
        }
        for (auto& st : prog.structs)
            if (st->name == sname)
                for (auto& f : st->fields)
                    cands.push_back({f.name, "field", typeStr(f.type, f.len, f.structName) +
                                                       (sname.empty() ? "" : " · " + sname)});
        return candsJson("member", prefix, cands);
    }

    // ---- 语境 2：类型位置（`:` 或 `->` 之后）→ 基本类型 + 结构体 ----
    std::string trimmed = head;
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();
    bool afterArrow = trimmed.size() >= 2 && trimmed.compare(trimmed.size() - 2, 2, "->") == 0;
    // `:` 之前若同行出现过 `{`，多半是结构体字面量 Point { x: … }，那是值位置不是类型位置
    bool afterColon = !trimmed.empty() && trimmed.back() == ':' &&
                      trimmed.find('{') == std::string::npos;
    if (afterArrow || afterColon) {
        for (auto* p : PRIMS) cands.push_back({p, "ty", "基本类型"});
        for (auto& st : prog.structs)
            cands.push_back({st->name, "ty", st->module.empty() ? "结构体" : "结构体 · " + st->module});
        int fi = enclosingFn(prog, line);
        if (fi >= 0) for (auto& tp : prog.fns[fi]->typeParams) cands.push_back({tp, "ty", "泛型参数"});
        return candsJson("type", prefix, cands);
    }

    // ---- 语境 3：普通标识符 → 关键字 + 作用域内变量 + 全部函数 + 结构体 ----
    for (auto* k : KEYWORDS) cands.push_back({k, "kw", "关键字"});
    for (auto* p : PRIMS) cands.push_back({p, "ty", "基本类型"});

    int fi = enclosingFn(prog, line);
    if (fi >= 0) {
        auto& f = *prog.fns[fi];
        for (auto& p : f.params) cands.push_back({p.name, "var", typeStr(p.type, p.len, p.structName) + " · 参数"});
        if (f.body) {
            std::vector<ScopeVar> vs;
            collectVars(*f.body, line, vs);
            for (auto& v : vs) cands.push_back({v.name, "var", v.typ});
        }
    }
    for (auto& g : prog.globals) {
        if (g->kind != StmtKind::Let) continue;
        auto& l = static_cast<const LetStmt&>(*g);
        Type t = l.declared != Type::Unknown ? l.declared : (l.init ? l.init->type : Type::Unknown);
        int len = l.declaredLen ? l.declaredLen : (l.init ? l.init->arrayLen : 0);
        std::string sn = !l.structName.empty() ? l.structName : (l.init ? l.init->structName : "");
        cands.push_back({l.name, "var", typeStr(t, len, sn) + " · 全局"});
    }
    for (auto& fp : prog.fns) {
        auto& f = *fp;
        if (isGenericInstance(prog, f)) continue;
        std::string d = fnSig(f);
        if (!f.module.empty()) d += " · " + f.module;
        else if (f.isExtern) d += " · 运行时";
        cands.push_back({f.name, "fn", d});
    }
    for (auto& st : prog.structs)
        cands.push_back({st->name, "ty", st->module.empty() ? "结构体" : "结构体 · " + st->module});
    cands.push_back({"print", "fn", "(x) 打印 · 运行时"});
    cands.push_back({"len", "fn", "(xs) -> int 长度 · 内置"});
    return candsJson("ident", prefix, cands);
}

} // namespace sincoding
