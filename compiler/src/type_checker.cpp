#include "type_checker.h"

namespace sincoding {

const char* typeName(Type t) {
    switch (t) {
        case Type::Int: return "int";
        case Type::Float: return "float";
        case Type::Bool: return "bool";
        case Type::Void: return "void";
        default: return "<unknown>";
    }
}

const char* typeToC(Type t) {
    switch (t) {
        case Type::Int: return "long long";
        case Type::Float: return "double";
        case Type::Bool: return "bool";
        case Type::Void: return "void";
        default: return "void";
    }
}

void TypeChecker::error(int line, const std::string& msg) {
    errors_.push_back({line, 0, msg});
}

bool TypeChecker::declare(const std::string& name, Type t) {
    auto& scope = scopes_.back();
    if (scope.count(name)) return false;
    scope[name] = t;
    return true;
}

Type TypeChecker::lookup(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    return Type::Unknown;
}

bool TypeChecker::check(Program& prog) {
    // 第一遍：收集函数签名（允许前向引用 / 互递归）
    for (auto& fn : prog.fns) {
        if (fns_.count(fn->name)) {
            error(fn->line, "函数重复定义: " + fn->name);
            continue;
        }
        FnSig sig;
        sig.ret = fn->ret;
        for (auto& p : fn->params) sig.params.push_back(p.type);
        fns_[fn->name] = sig;
    }
    // 必须有 main
    if (!fns_.count("main"))
        error(0, "缺少入口函数 main");

    // 第二遍：检查函数体
    for (auto& fn : prog.fns) checkFn(*fn);
    return errors_.empty();
}

void TypeChecker::checkFn(FnDecl& fn) {
    curRet_ = fn.ret;
    pushScope();
    for (auto& p : fn.params) {
        if (p.type == Type::Void)
            error(p.line, "参数 '" + p.name + "' 不能是 void 类型");
        if (!declare(p.name, p.type))
            error(p.line, "参数名重复: " + p.name);
    }
    checkBlock(*fn.body);
    popScope();
}

void TypeChecker::checkBlock(Block& block) {
    pushScope();
    for (auto& s : block.stmts) checkStmt(*s);
    popScope();
}

void TypeChecker::checkStmt(Stmt& s) {
    switch (s.kind) {
        case StmtKind::Let: {
            auto& ls = static_cast<LetStmt&>(s);
            Type initT = checkExpr(*ls.init);
            if (ls.declared == Type::Unknown) {
                // 局部推断
                if (initT == Type::Void)
                    error(ls.line, "无法从 void 表达式推断 'let " + ls.name + "' 的类型");
                ls.declared = (initT == Type::Void) ? Type::Int : initT;
            } else if (initT != Type::Unknown && initT != ls.declared) {
                error(ls.line, "类型不匹配: 'let " + ls.name + ": " +
                                   typeName(ls.declared) + "' 不能用 " +
                                   typeName(initT) + " 初始化");
            }
            if (!declare(ls.name, ls.declared))
                error(ls.line, "变量重复定义: " + ls.name);
            break;
        }
        case StmtKind::Assign: {
            auto& as = static_cast<AssignStmt&>(s);
            Type varT = lookup(as.name);
            if (varT == Type::Unknown) {
                error(as.line, "赋值给未声明的变量: " + as.name);
            }
            Type valT = checkExpr(*as.value);
            if (varT != Type::Unknown && valT != Type::Unknown && varT != valT)
                error(as.line, "赋值类型不匹配: " + as.name + " 是 " +
                                   typeName(varT) + "，却赋以 " + typeName(valT));
            break;
        }
        case StmtKind::If: {
            auto& is = static_cast<IfStmt&>(s);
            Type c = checkExpr(*is.cond);
            if (c != Type::Bool && c != Type::Unknown)
                error(is.line, "if 条件必须是 bool，而非 " + std::string(typeName(c)));
            checkBlock(*is.thenBlock);
            if (is.elseBlock) checkBlock(*is.elseBlock);
            break;
        }
        case StmtKind::While: {
            auto& ws = static_cast<WhileStmt&>(s);
            Type c = checkExpr(*ws.cond);
            if (c != Type::Bool && c != Type::Unknown)
                error(ws.line, "while 条件必须是 bool，而非 " + std::string(typeName(c)));
            checkBlock(*ws.body);
            break;
        }
        case StmtKind::Return: {
            auto& rs = static_cast<ReturnStmt&>(s);
            if (rs.value) {
                Type vt = checkExpr(*rs.value);
                if (curRet_ == Type::Void)
                    error(rs.line, "void 函数不能返回值");
                else if (vt != Type::Unknown && vt != curRet_)
                    error(rs.line, "返回类型不匹配: 期望 " +
                                       std::string(typeName(curRet_)) + "，得到 " +
                                       typeName(vt));
            } else if (curRet_ != Type::Void) {
                error(rs.line, "非 void 函数必须返回 " +
                                   std::string(typeName(curRet_)) + " 值");
            }
            break;
        }
        case StmtKind::ExprStmt: {
            auto& es = static_cast<ExprStmt&>(s);
            checkExpr(*es.expr);
            break;
        }
        case StmtKind::Block:
            checkBlock(static_cast<Block&>(s));
            break;
    }
}

Type TypeChecker::checkExpr(Expr& e) {
    switch (e.kind) {
        case ExprKind::IntLit: e.type = Type::Int; return Type::Int;
        case ExprKind::FloatLit: e.type = Type::Float; return Type::Float;
        case ExprKind::BoolLit: e.type = Type::Bool; return Type::Bool;
        case ExprKind::Var: {
            auto& v = static_cast<Var&>(e);
            Type t = lookup(v.name);
            if (t == Type::Unknown)
                error(v.line, "使用了未声明的变量: " + v.name);
            v.type = t;
            return t;
        }
        case ExprKind::Unary: {
            auto& u = static_cast<Unary&>(e);
            Type ot = checkExpr(*u.operand);
            if (u.op == "-") {
                if (ot != Type::Int && ot != Type::Float && ot != Type::Unknown)
                    error(u.line, "一元 '-' 需要数值类型，而非 " + std::string(typeName(ot)));
                u.type = ot;
            } else { // "!"
                if (ot != Type::Bool && ot != Type::Unknown)
                    error(u.line, "'!' 需要 bool 类型，而非 " + std::string(typeName(ot)));
                u.type = Type::Bool;
            }
            return u.type;
        }
        case ExprKind::Binary: {
            auto& b = static_cast<Binary&>(e);
            Type lt = checkExpr(*b.lhs);
            Type rt = checkExpr(*b.rhs);
            const std::string& op = b.op;
            if (op == "&&" || op == "||") {
                if (lt != Type::Bool && lt != Type::Unknown)
                    error(b.line, "'" + op + "' 左侧需要 bool");
                if (rt != Type::Bool && rt != Type::Unknown)
                    error(b.line, "'" + op + "' 右侧需要 bool");
                b.type = Type::Bool;
            } else if (op == "==" || op == "!=" || op == "<" || op == "<=" ||
                       op == ">" || op == ">=") {
                if (lt != Type::Unknown && rt != Type::Unknown && lt != rt)
                    error(b.line, "比较运算 '" + op + "' 两侧类型不一致: " +
                                      typeName(lt) + " 与 " + typeName(rt));
                b.type = Type::Bool;
            } else { // + - * / %
                if (lt != Type::Unknown && rt != Type::Unknown && lt != rt)
                    error(b.line, "算术运算 '" + op + "' 两侧类型不一致: " +
                                      typeName(lt) + " 与 " + typeName(rt));
                if ((lt == Type::Bool || rt == Type::Bool))
                    error(b.line, "算术运算 '" + op + "' 不能用于 bool");
                if (op == "%" && (lt == Type::Float || rt == Type::Float))
                    error(b.line, "'%' 不能用于 float");
                b.type = (lt != Type::Unknown) ? lt : rt;
            }
            return b.type;
        }
        case ExprKind::Call: {
            auto& c = static_cast<Call&>(e);
            // 内建 print：接受单个 int/float/bool，返回 void
            if (c.callee == "print") {
                if (c.args.size() != 1) {
                    error(c.line, "print 需要恰好 1 个参数");
                } else {
                    Type at = checkExpr(*c.args[0]);
                    if (at == Type::Void)
                        error(c.line, "print 不能打印 void");
                }
                c.type = Type::Void;
                return Type::Void;
            }
            auto it = fns_.find(c.callee);
            if (it == fns_.end()) {
                error(c.line, "调用了未定义的函数: " + c.callee);
                for (auto& a : c.args) checkExpr(*a);
                c.type = Type::Unknown;
                return Type::Unknown;
            }
            const FnSig& sig = it->second;
            if (c.args.size() != sig.params.size())
                error(c.line, "函数 " + c.callee + " 期望 " +
                                  std::to_string(sig.params.size()) + " 个参数，得到 " +
                                  std::to_string(c.args.size()));
            for (size_t i = 0; i < c.args.size(); i++) {
                Type at = checkExpr(*c.args[i]);
                if (i < sig.params.size() && at != Type::Unknown &&
                    at != sig.params[i])
                    error(c.line, "函数 " + c.callee + " 第 " + std::to_string(i + 1) +
                                      " 个参数类型应为 " + typeName(sig.params[i]) +
                                      "，得到 " + typeName(at));
            }
            c.type = sig.ret;
            return sig.ret;
        }
    }
    return Type::Unknown;
}

} // namespace sincoding
