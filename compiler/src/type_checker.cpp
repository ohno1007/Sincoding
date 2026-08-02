#include "type_checker.h"
#include <unordered_set>

namespace sincoding {

const char* typeName(Type t) {
    switch (t) {
        case Type::Int: return "int";
        case Type::Float: return "float";
        case Type::Bool: return "bool";
        case Type::String: return "string";
        case Type::Struct: return "struct";
        case Type::Void: return "void";
        default: return "<unknown>";
    }
}

const char* typeToC(Type t) {
    switch (t) {
        case Type::Int: return "long long";
        case Type::Float: return "double";
        case Type::Bool: return "bool";
        case Type::String: return "const char*";
        case Type::Void: return "void";
        default: return "void";
    }
}

void TypeChecker::error(int line, const std::string& msg) {
    errors_.push_back({line, 0, msg});
}

bool TypeChecker::declare(const std::string& name, VarType t) {
    auto& scope = scopes_.back();
    if (scope.count(name)) return false;
    scope[name] = t;
    return true;
}

VarType TypeChecker::lookup(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    return {Type::Unknown, 0, ""};
}

// 把 (base,len) 渲染成可读类型名，如 "int" 或 "int[8]"
static std::string typeStr(Type base, int len) {
    std::string s = typeName(base);
    if (len > 0) s += "[" + std::to_string(len) + "]";
    return s;
}
// 含结构体名的类型渲染
static std::string declTypeStr(Type base, int len, const std::string& sn) {
    if (base == Type::Struct) return sn.empty() ? "struct" : sn;
    return typeStr(base, len);
}

bool TypeChecker::check(Program& prog) {
    // 0) 结构体声明（字段必须是标量）
    for (auto& st : prog.structs) {
        if (structs_.count(st->name)) {
            error(st->line, "结构体重复定义: " + st->name);
            continue;
        }
        std::unordered_set<std::string> seen;
        for (auto& f : st->fields) {
            if (f.type == Type::Struct)
                error(f.line, "字段 '" + f.name + "' 暂不支持结构体类型（仅标量）");
            else if (f.type == Type::Void)
                error(f.line, "字段 '" + f.name + "' 不能是 void");
            if (!seen.insert(f.name).second)
                error(f.line, "字段名重复: " + f.name);
        }
        structs_[st->name] = st->fields;
    }

    // 1) 函数签名（允许前向引用 / 互递归；参数/返回可为结构体）
    for (auto& fn : prog.fns) {
        if (fns_.count(fn->name)) {
            error(fn->line, "函数重复定义: " + fn->name);
            continue;
        }
        FnSig sig;
        sig.ret = {fn->ret, 0, fn->retStruct};
        if (fn->ret == Type::Struct && !structs_.count(fn->retStruct))
            error(fn->line, "未定义的结构体: " + fn->retStruct);
        for (auto& p : fn->params) {
            if (p.type == Type::Struct && !structs_.count(p.structName))
                error(p.line, "未定义的结构体: " + p.structName);
            sig.params.push_back({p.type, 0, p.structName});
        }
        fns_[fn->name] = sig;
    }
    // 必须有 main
    if (!fns_.count("main"))
        error(0, "缺少入口函数 main");

    // 全局变量在最外层作用域，函数体内可见
    pushScope();
    for (auto& g : prog.globals) checkStmt(*g);

    // 第二遍：检查函数体（extern 声明无函数体，跳过）
    for (auto& fn : prog.fns)
        if (!fn->isExtern) checkFn(*fn);
    popScope();
    return errors_.empty();
}

void TypeChecker::checkFn(FnDecl& fn) {
    curRet_ = fn.ret;
    curRetStruct_ = fn.retStruct;
    pushScope();
    for (auto& p : fn.params) {
        if (p.type == Type::Void)
            error(p.line, "参数 '" + p.name + "' 不能是 void 类型");
        if (!declare(p.name, {p.type, 0, p.structName}))
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
            if (ls.init) {
                Type initT = checkExpr(*ls.init);
                int initLen = ls.init->arrayLen;
                if (ls.declared == Type::Unknown && ls.declaredLen == 0) {
                    // 完全推断
                    if (initT == Type::Void)
                        error(ls.line, "无法从 void 表达式推断 'let " + ls.name + "' 的类型");
                    ls.declared = (initT == Type::Void) ? Type::Int : initT;
                    ls.declaredLen = initLen;
                    ls.structName = ls.init->structName;
                } else if (initT != Type::Unknown) {
                    bool ok = (initT == ls.declared) && (ls.declaredLen == initLen) &&
                              (ls.declared != Type::Struct || ls.init->structName == ls.structName);
                    if (!ok)
                        error(ls.line, "类型不匹配: 'let " + ls.name + ": " +
                                           declTypeStr(ls.declared, ls.declaredLen, ls.structName) +
                                           "' 不能用 " +
                                           declTypeStr(initT, initLen, ls.init->structName) + " 初始化");
                }
            } else if (ls.declared == Type::Unknown) {
                error(ls.line, "无初始化的 'let " + ls.name + "' 必须标注类型");
            }
            if (ls.declared == Type::Struct && !structs_.count(ls.structName))
                error(ls.line, "未定义的结构体: " + ls.structName);
            if (!declare(ls.name, {ls.declared, ls.declaredLen, ls.structName}))
                error(ls.line, "变量重复定义: " + ls.name);
            break;
        }
        case StmtKind::Assign: {
            auto& as = static_cast<AssignStmt&>(s);
            VarType vt = lookup(as.name);
            if (vt.base == Type::Unknown)
                error(as.line, "赋值给未声明的变量: " + as.name);
            if (!as.field.empty()) {
                // 字段赋值 name.field = value
                Type ft = Type::Unknown;
                if (vt.base != Type::Struct && vt.base != Type::Unknown) {
                    error(as.line, as.name + " 不是结构体，不能用 '." + as.field + "' 赋值");
                } else {
                    bool found = false;
                    auto sit = structs_.find(vt.structName);
                    if (sit != structs_.end())
                        for (auto& d : sit->second) if (d.name == as.field) { ft = d.type; found = true; }
                    if (!found && vt.base == Type::Struct)
                        error(as.line, "结构体 " + vt.structName + " 没有字段 '" + as.field + "'");
                }
                Type valT = checkExpr(*as.value);
                if (as.value->arrayLen != 0 || valT == Type::Struct)
                    error(as.line, "字段只能赋标量值");
                else if (valT != Type::Unknown && ft != Type::Unknown && valT != ft)
                    error(as.line, "字段 '" + as.field + "' 类型不匹配: 应为 " +
                                       typeName(ft) + "，得到 " + typeName(valT));
            } else if (as.index) {
                // 元素赋值 name[idx] = value
                if (vt.len == 0 && vt.base != Type::Unknown)
                    error(as.line, as.name + " 不是数组，不能用下标赋值");
                Type it = checkExpr(*as.index);
                if (it != Type::Int && it != Type::Unknown)
                    error(as.line, "数组下标必须是 int，而非 " + std::string(typeName(it)));
                Type valT = checkExpr(*as.value);
                if (as.value->arrayLen != 0)
                    error(as.line, "不能把数组赋给单个元素");
                else if (valT != Type::Unknown && vt.base != Type::Unknown && valT != vt.base)
                    error(as.line, "元素类型不匹配: " + as.name + " 的元素是 " +
                                       typeName(vt.base) + "，却赋以 " + typeName(valT));
            } else {
                Type valT = checkExpr(*as.value);
                int valLen = as.value->arrayLen;
                bool ok = (valT == vt.base) && (valLen == vt.len) &&
                          (vt.base != Type::Struct || as.value->structName == vt.structName);
                if (vt.base != Type::Unknown && valT != Type::Unknown && !ok)
                    error(as.line, "赋值类型不匹配: " + as.name + " 是 " +
                                       declTypeStr(vt.base, vt.len, vt.structName) + "，却赋以 " +
                                       declTypeStr(valT, valLen, as.value->structName));
            }
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
        case StmtKind::For: {
            auto& fs = static_cast<ForStmt&>(s);
            Type st = checkExpr(*fs.start);
            if ((st != Type::Int && st != Type::Unknown) || fs.start->arrayLen != 0)
                error(fs.line, "for 起始值必须是 int");
            Type et = checkExpr(*fs.end);
            if ((et != Type::Int && et != Type::Unknown) || fs.end->arrayLen != 0)
                error(fs.line, "for 结束值必须是 int");
            pushScope();
            declare(fs.var, {Type::Int, 0, ""});  // 循环变量
            checkBlock(*fs.body);
            popScope();
            break;
        }
        case StmtKind::Return: {
            auto& rs = static_cast<ReturnStmt&>(s);
            if (rs.value) {
                Type vt = checkExpr(*rs.value);
                if (rs.value->arrayLen != 0)
                    error(rs.line, "不能返回数组");
                if (curRet_ == Type::Void)
                    error(rs.line, "void 函数不能返回值");
                else if (vt != Type::Unknown &&
                         (vt != curRet_ ||
                          (curRet_ == Type::Struct && rs.value->structName != curRetStruct_)))
                    error(rs.line, "返回类型不匹配: 期望 " +
                                       declTypeStr(curRet_, 0, curRetStruct_) + "，得到 " +
                                       declTypeStr(vt, 0, rs.value->structName));
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
        case ExprKind::StringLit: e.type = Type::String; return Type::String;
        case ExprKind::Var: {
            auto& v = static_cast<Var&>(e);
            VarType t = lookup(v.name);
            if (t.base == Type::Unknown)
                error(v.line, "使用了未声明的变量: " + v.name);
            v.type = t.base;
            v.arrayLen = t.len;
            v.structName = t.structName;
            return t.base;
        }
        case ExprKind::StructLit: {
            auto& sl = static_cast<StructLit&>(e);
            sl.type = Type::Struct;
            sl.structName = sl.typeName;
            auto it = structs_.find(sl.typeName);
            if (it == structs_.end()) {
                error(sl.line, "未定义的结构体: " + sl.typeName);
                for (auto& fi : sl.fields) checkExpr(*fi.value);
                return Type::Struct;
            }
            const auto& fields = it->second;
            std::unordered_set<std::string> given;
            for (auto& fi : sl.fields) {
                Type vt = checkExpr(*fi.value);
                const StructField* def = nullptr;
                for (auto& d : fields) if (d.name == fi.name) { def = &d; break; }
                if (!def) { error(sl.line, sl.typeName + " 没有字段 '" + fi.name + "'"); continue; }
                if (!given.insert(fi.name).second)
                    error(sl.line, "字段 '" + fi.name + "' 重复赋值");
                if (fi.value->arrayLen != 0 || vt == Type::Struct)
                    error(sl.line, "字段 '" + fi.name + "' 只能是标量值");
                else if (vt != Type::Unknown && vt != def->type)
                    error(sl.line, "字段 '" + fi.name + "' 类型应为 " +
                                       typeName(def->type) + "，得到 " + typeName(vt));
            }
            if (given.size() != fields.size())
                error(sl.line, "结构体 " + sl.typeName + " 需要初始化全部 " +
                                   std::to_string(fields.size()) + " 个字段");
            return Type::Struct;
        }
        case ExprKind::Field: {
            auto& fa = static_cast<FieldAccess&>(e);
            Type ot = checkExpr(*fa.obj);
            if (ot != Type::Struct) {
                if (ot != Type::Unknown)
                    error(fa.line, "'." + fa.field + "' 只能用于结构体");
                fa.type = Type::Unknown;
                return Type::Unknown;
            }
            auto it = structs_.find(fa.obj->structName);
            if (it != structs_.end()) {
                for (auto& d : it->second) if (d.name == fa.field) {
                    fa.type = d.type; fa.arrayLen = 0; return d.type;
                }
            }
            error(fa.line, "结构体 " + fa.obj->structName + " 没有字段 '" + fa.field + "'");
            fa.type = Type::Unknown;
            return Type::Unknown;
        }
        case ExprKind::Index: {
            auto& ix = static_cast<IndexExpr&>(e);
            Type at = checkExpr(*ix.arr);
            if (ix.arr->arrayLen == 0 && at != Type::Unknown)
                error(ix.line, "下标访问的不是数组");
            Type it = checkExpr(*ix.idx);
            if (it != Type::Int && it != Type::Unknown)
                error(ix.line, "数组下标必须是 int，而非 " + std::string(typeName(it)));
            ix.type = at;                     // 元素类型
            ix.structName = ix.arr->structName; // 结构体数组：元素携带结构体名
            ix.arrayLen = 0;
            return at;
        }
        case ExprKind::ArrayLit: {
            auto& al = static_cast<ArrayLit&>(e);
            if (al.elems.empty()) {
                error(al.line, "数组字面量不能为空");
                al.type = Type::Int; al.arrayLen = 0; return Type::Int;
            }
            Type elemT = checkExpr(*al.elems[0]);
            for (size_t i = 1; i < al.elems.size(); i++) {
                Type t = checkExpr(*al.elems[i]);
                if (t != Type::Unknown && elemT != Type::Unknown && t != elemT)
                    error(al.line, "数组元素类型不一致: " + std::string(typeName(elemT)) +
                                       " 与 " + typeName(t));
            }
            for (auto& el : al.elems)
                if (el->arrayLen != 0) error(al.line, "不支持嵌套数组");
            al.type = elemT;
            al.arrayLen = (int)al.elems.size();
            return elemT;
        }
        case ExprKind::Unary: {
            auto& u = static_cast<Unary&>(e);
            Type ot = checkExpr(*u.operand);
            if (u.operand->arrayLen != 0 || ot == Type::Struct)
                error(u.line, "一元运算 '" + u.op + "' 不能用于数组/结构体");
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
            if (b.lhs->arrayLen != 0 || b.rhs->arrayLen != 0 ||
                lt == Type::Struct || rt == Type::Struct)
                error(b.line, "运算 '" + b.op + "' 不能用于数组/结构体");
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
                // 字符串仅支持相等/不等比较（== / !=），不支持大小比较
                if ((lt == Type::String || rt == Type::String) &&
                    op != "==" && op != "!=")
                    error(b.line, "string 仅支持 == / != 比较，不支持 '" + op + "'");
                b.type = Type::Bool;
            } else { // + - * / %
                if (lt != Type::Unknown && rt != Type::Unknown && lt != rt)
                    error(b.line, "算术运算 '" + op + "' 两侧类型不一致: " +
                                      typeName(lt) + " 与 " + typeName(rt));
                if (lt == Type::Bool || rt == Type::Bool)
                    error(b.line, "算术运算 '" + op + "' 不能用于 bool");
                if (lt == Type::String || rt == Type::String)
                    error(b.line, "算术运算 '" + op + "' 不能用于 string（暂不支持拼接）");
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
                    if (c.args[0]->arrayLen != 0)
                        error(c.line, "print 不能打印数组");
                    if (at == Type::Struct)
                        error(c.line, "print 不能打印结构体");
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
                if (c.args[i]->arrayLen != 0)
                    error(c.line, "不能把数组作为参数传递");
                if (i < sig.params.size()) {
                    const VarType& pt = sig.params[i];
                    bool ok = (at == pt.base) &&
                              (pt.base != Type::Struct || c.args[i]->structName == pt.structName);
                    if (at != Type::Unknown && !ok)
                        error(c.line, "函数 " + c.callee + " 第 " + std::to_string(i + 1) +
                                          " 个参数类型应为 " + declTypeStr(pt.base, pt.len, pt.structName) +
                                          "，得到 " + declTypeStr(at, c.args[i]->arrayLen, c.args[i]->structName));
                }
            }
            c.type = sig.ret.base;
            c.structName = sig.ret.structName;
            c.arrayLen = sig.ret.len;
            return sig.ret.base;
        }
    }
    return Type::Unknown;
}

} // namespace sincoding
