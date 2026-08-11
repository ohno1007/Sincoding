// generics.cpp — 泛型单态化实现（详见 generics.h）
#include "generics.h"
#include "type_checker.h"

namespace sincoding {
namespace {

// ---- 类型替换：structName 命中类型参数则换成实参 ----
void substType(Type& t, std::string& sn, const std::map<std::string, TypeArg>& subst) {
    if (t != Type::Struct || sn.empty()) return;
    auto it = subst.find(sn);
    if (it == subst.end()) return;          // 不是类型参数：真结构体，原样保留
    t = it->second.base;
    sn = (it->second.base == Type::Struct) ? it->second.structName : "";
}

// ---- AST 深拷贝（克隆时同步做类型替换）----
using Subst = std::map<std::string, TypeArg>;

ExprPtr cloneExpr(const Expr& e, const Subst& sb);
StmtPtr cloneStmt(const Stmt& s, const Subst& sb);

template <typename T>
void copyExprBase(T& dst, const Expr& src) {
    dst.type = src.type; dst.arrayLen = src.arrayLen; dst.structName = src.structName;
    dst.line = src.line; dst.col = src.col;
}

ExprPtr cloneExpr(const Expr& e, const Subst& sb) {
    switch (e.kind) {
        case ExprKind::IntLit: {
            auto n = std::make_unique<IntLit>(); copyExprBase(*n, e);
            n->value = static_cast<const IntLit&>(e).value; return n;
        }
        case ExprKind::FloatLit: {
            auto n = std::make_unique<FloatLit>(); copyExprBase(*n, e);
            n->value = static_cast<const FloatLit&>(e).value; return n;
        }
        case ExprKind::BoolLit: {
            auto n = std::make_unique<BoolLit>(); copyExprBase(*n, e);
            n->value = static_cast<const BoolLit&>(e).value; return n;
        }
        case ExprKind::StringLit: {
            auto n = std::make_unique<StringLit>(); copyExprBase(*n, e);
            n->value = static_cast<const StringLit&>(e).value; return n;
        }
        case ExprKind::Var: {
            auto n = std::make_unique<Var>(); copyExprBase(*n, e);
            n->name = static_cast<const Var&>(e).name; return n;
        }
        case ExprKind::Unary: {
            auto& u = static_cast<const Unary&>(e);
            auto n = std::make_unique<Unary>(); copyExprBase(*n, e);
            n->op = u.op; n->operand = cloneExpr(*u.operand, sb); return n;
        }
        case ExprKind::Binary: {
            auto& b = static_cast<const Binary&>(e);
            auto n = std::make_unique<Binary>(); copyExprBase(*n, e);
            n->op = b.op; n->lhs = cloneExpr(*b.lhs, sb); n->rhs = cloneExpr(*b.rhs, sb); return n;
        }
        case ExprKind::Call: {
            auto& c = static_cast<const Call&>(e);
            auto n = std::make_unique<Call>(); copyExprBase(*n, e);
            n->callee = c.callee; n->resolved = c.resolved;
            for (auto& a : c.args) n->args.push_back(cloneExpr(*a, sb));
            return n;
        }
        case ExprKind::Index: {
            auto& x = static_cast<const IndexExpr&>(e);
            auto n = std::make_unique<IndexExpr>(); copyExprBase(*n, e);
            n->arr = cloneExpr(*x.arr, sb); n->idx = cloneExpr(*x.idx, sb); return n;
        }
        case ExprKind::ArrayLit: {
            auto& a = static_cast<const ArrayLit&>(e);
            auto n = std::make_unique<ArrayLit>(); copyExprBase(*n, e);
            for (auto& el : a.elems) n->elems.push_back(cloneExpr(*el, sb));
            return n;
        }
        case ExprKind::Field: {
            auto& f = static_cast<const FieldAccess&>(e);
            auto n = std::make_unique<FieldAccess>(); copyExprBase(*n, e);
            n->obj = cloneExpr(*f.obj, sb); n->field = f.field; return n;
        }
        case ExprKind::StructLit: {
            auto& sl = static_cast<const StructLit&>(e);
            auto n = std::make_unique<StructLit>(); copyExprBase(*n, e);
            n->typeName = sl.typeName;
            // 结构体字面量的类型名也可能是类型参数（如 T { ... } 少见但允许）
            { Type t = Type::Struct; std::string sn = n->typeName; substType(t, sn, sb);
              if (t == Type::Struct && !sn.empty()) n->typeName = sn; }
            for (auto& fi : sl.fields) {
                FieldInit ni; ni.name = fi.name; ni.value = cloneExpr(*fi.value, sb);
                n->fields.push_back(std::move(ni));
            }
            return n;
        }
    }
    return nullptr;
}

BlockPtr cloneBlock(const Block& b, const Subst& sb) {
    auto n = std::make_unique<Block>();
    n->line = b.line; n->col = b.col; n->module = b.module;
    for (auto& s : b.stmts) n->stmts.push_back(cloneStmt(*s, sb));
    return n;
}

template <typename T>
void copyStmtBase(T& dst, const Stmt& src) {
    dst.line = src.line; dst.col = src.col; dst.module = src.module;
}

StmtPtr cloneStmt(const Stmt& s, const Subst& sb) {
    switch (s.kind) {
        case StmtKind::Let: {
            auto& l = static_cast<const LetStmt&>(s);
            auto n = std::make_unique<LetStmt>(); copyStmtBase(*n, s);
            n->name = l.name; n->declared = l.declared; n->declaredLen = l.declaredLen;
            n->structName = l.structName; n->isConst = l.isConst;
            substType(n->declared, n->structName, sb);      // let x: T
            if (l.init) n->init = cloneExpr(*l.init, sb);
            return n;
        }
        case StmtKind::Assign: {
            auto& a = static_cast<const AssignStmt&>(s);
            auto n = std::make_unique<AssignStmt>(); copyStmtBase(*n, s);
            n->name = a.name; n->field = a.field;
            if (a.index) n->index = cloneExpr(*a.index, sb);
            n->value = cloneExpr(*a.value, sb);
            return n;
        }
        case StmtKind::If: {
            auto& i = static_cast<const IfStmt&>(s);
            auto n = std::make_unique<IfStmt>(); copyStmtBase(*n, s);
            n->cond = cloneExpr(*i.cond, sb);
            n->thenBlock = cloneBlock(*i.thenBlock, sb);
            if (i.elseBlock) n->elseBlock = cloneBlock(*i.elseBlock, sb);
            return n;
        }
        case StmtKind::While: {
            auto& w = static_cast<const WhileStmt&>(s);
            auto n = std::make_unique<WhileStmt>(); copyStmtBase(*n, s);
            n->cond = cloneExpr(*w.cond, sb); n->body = cloneBlock(*w.body, sb);
            return n;
        }
        case StmtKind::For: {
            auto& f = static_cast<const ForStmt&>(s);
            auto n = std::make_unique<ForStmt>(); copyStmtBase(*n, s);
            n->var = f.var; n->start = cloneExpr(*f.start, sb); n->end = cloneExpr(*f.end, sb);
            n->body = cloneBlock(*f.body, sb);
            return n;
        }
        case StmtKind::Return: {
            auto& r = static_cast<const ReturnStmt&>(s);
            auto n = std::make_unique<ReturnStmt>(); copyStmtBase(*n, s);
            if (r.value) n->value = cloneExpr(*r.value, sb);
            return n;
        }
        case StmtKind::Break: {
            auto n = std::make_unique<BreakStmt>(); copyStmtBase(*n, s);
            return n;
        }
        case StmtKind::Continue: {
            auto n = std::make_unique<ContinueStmt>(); copyStmtBase(*n, s);
            return n;
        }
        case StmtKind::ExprStmt: {
            auto& e = static_cast<const ExprStmt&>(s);
            auto n = std::make_unique<ExprStmt>(); copyStmtBase(*n, s);
            n->expr = cloneExpr(*e.expr, sb);
            return n;
        }
        case StmtKind::Block:
            return cloneBlock(static_cast<const Block&>(s), sb);
    }
    return nullptr;
}

} // namespace

std::string mangleName(const std::string& fnName, const std::vector<std::string>& typeParams,
                       const std::map<std::string, TypeArg>& subst) {
    std::string out = fnName;
    for (auto& tp : typeParams) {
        auto it = subst.find(tp);
        out += "__";
        if (it == subst.end()) { out += "x"; continue; }
        out += (it->second.base == Type::Struct) ? it->second.structName : typeName(it->second.base);
    }
    return out;
}

FnPtr instantiateFn(const FnDecl& gen, const std::map<std::string, TypeArg>& subst,
                    const std::string& mangled) {
    auto fn = std::make_unique<FnDecl>();
    fn->name = mangled;
    fn->ret = gen.ret; fn->retLen = gen.retLen; fn->retStruct = gen.retStruct;
    substType(fn->ret, fn->retStruct, subst);
    fn->isExtern = gen.isExtern;
    fn->line = gen.line; fn->col = gen.col;
    // 实例是合成产物：标记为「非用户源码」，序列化/积木视图会跳过它
    fn->module = gen.module.empty() ? "<generic>" : gen.module;
    for (auto& p : gen.params) {
        Param np = p;
        substType(np.type, np.structName, subst);
        fn->params.push_back(np);
    }
    if (gen.body) fn->body = cloneBlock(*gen.body, subst);
    return fn;
}

bool checkWithGenerics(Program& prog, std::vector<Diagnostic>& outDiags) {
    const int kMaxRounds = 8;          // 实例再调用泛型时需多轮；给个上限防失控
    for (int round = 0; round < kMaxRounds; round++) {
        TypeChecker tc;
        bool ok = tc.check(prog);
        const auto& reqs = tc.instantiations();
        // 本轮没有新实例：结果即为最终结果
        bool added = false;
        for (auto& r : reqs) {
            bool exists = false;
            for (auto& fn : prog.fns) if (fn->name == r.mangled) { exists = true; break; }
            if (exists) continue;
            prog.fns.push_back(instantiateFn(*r.gen, r.subst, r.mangled));
            added = true;
        }
        if (!added) { outDiags = tc.errors(); return ok; }
    }
    outDiags.push_back({0, 0, "泛型实例化层数过深（可能是无限递归的泛型调用）"});
    return false;
}

} // namespace sincoding
