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
// 带列号的诊断：编辑器据此把波浪线画在**出错的那个标识符/表达式**上，
// 而不是整行高亮。AST 节点的 col 是为 mini-LSP 加的，这里正好复用。
void TypeChecker::errorAt(const Expr& e, const std::string& msg) {
    errors_.push_back({e.line, e.col, msg});
}
void TypeChecker::errorAt(const Stmt& st, const std::string& msg) {
    errors_.push_back({st.line, st.col, msg});
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
    else if (len == -1) s += "[]";               // 切片
    return s;
}
// 含结构体名的类型渲染
static std::string declTypeStr(Type base, int len, const std::string& sn) {
    if (base == Type::Struct) {
        std::string s = sn.empty() ? "struct" : sn;
        if (len > 0) s += "[" + std::to_string(len) + "]";
        else if (len == -1) s += "[]";
        return s;
    }
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
            if (f.type == Type::Void)
                error(f.line, "字段 '" + f.name + "' 不能是 void");
            else if (f.len == -1)
                error(f.line, "字段 '" + f.name + "' 不能是切片 T[]（切片是借用视图，存进结构体会悬垂）");
            else if (f.type == Type::Struct) {
                // 结构体字段：必须是「已在前面声明」的其它结构体（防环，满足 C 顺序）
                if (f.len > 0)
                    error(f.line, "字段 '" + f.name + "' 暂不支持结构体数组（可用标量数组或结构体字段）");
                else if (f.structName == st->name)
                    error(f.line, "字段 '" + f.name + "' 不能是所属结构体自身（无指针，禁止递归）");
                else if (!structs_.count(f.structName))
                    error(f.line, "字段 '" + f.name + "' 引用了未定义或未在前面声明的结构体: " + f.structName);
            }
            if (f.len == -2)
                error(f.line, "字段 '" + f.name + "' 不能是列表 T[*]（列表请用全局变量）");
            if (!seen.insert(f.name).second)
                error(f.line, "字段名重复: " + f.name);
        }
        structs_[st->name] = st->fields;
    }

    // 1) 函数签名（允许前向引用 / 互递归；参数/返回可为结构体）
    for (auto& fn : prog.fns) {
        if (!fn->typeParams.empty()) {          // 泛型模板：留待调用点单态化
            generics_[fn->name] = fn.get();
            continue;
        }
        if (fns_.count(fn->name)) {
            error(fn->line, "函数重复定义: " + fn->name);
            continue;
        }
        FnSig sig;
        if (fn->retLen == -1)
            error(fn->line, "函数 " + fn->name + " 不能返回切片 T[]（会指向已销毁的局部数组）");
        if (fn->retLen == -2)
            error(fn->line, "函数 " + fn->name + " 不能返回列表 T[*]（往全局列表里 push 即可）");
        sig.ret = {fn->ret, fn->retLen, fn->retStruct};
        if (fn->ret == Type::Struct && !structs_.count(fn->retStruct))
            error(fn->line, "未定义的结构体: " + fn->retStruct);
        for (auto& p : fn->params) {
            if (p.type == Type::Struct && !structs_.count(p.structName))
                error(p.line, "未定义的结构体: " + p.structName);
            sig.params.push_back({p.type, p.len, p.structName});
        }
        fns_[fn->name] = sig;
    }
    // 必须有 main，或者至少一个事件处理函数（on_start/on_frame/on_key_* 等，
    // 见 codegen 的事件驱动：没写 main 时编译器会合成主循环驱动）
    bool hasEntry = fns_.count("main") > 0;
    for (auto& fn : prog.fns)
        if (!hasEntry && fn->name.rfind("on_", 0) == 0 && !fn->isExtern) hasEntry = true;
    if (!hasEntry)
        error(0, "缺少入口：需要 main 函数，或至少一个事件函数（on_start / on_frame / on_key_空格键 等）");
    // 事件函数必须是无参 void：驱动器不带实参调用它们
    for (auto& fn : prog.fns) {
        if (fn->isExtern || fn->name.rfind("on_", 0) != 0) continue;
        static const char* EVENTS[] = {"on_start", "on_frame", "on_key_space", "on_key_left",
                                       "on_key_right", "on_key_up", "on_key_down", "on_click"};
        bool known = false;
        for (auto* e : EVENTS) if (fn->name == e) known = true;
        if (!known) continue;                      // 其它 on_ 开头的名字当普通函数
        if (!fn->params.empty() || fn->ret != Type::Void)
            error(fn->line, "事件函数 " + fn->name + " 必须没有参数且不返回值（fn " + fn->name + "() { ... }）");
    }

    // 全局变量在最外层作用域，函数体内可见
    pushScope();
    for (auto& g : prog.globals) checkStmt(*g);

    // 第二遍：检查函数体（extern 声明无函数体，跳过）
    for (auto& fn : prog.fns)
        if (!fn->isExtern && fn->typeParams.empty()) checkFn(*fn);
    popScope();
    return errors_.empty();
}

void TypeChecker::checkFn(FnDecl& fn) {
    inFn_ = true;
    curRet_ = fn.ret;
    curRetLen_ = fn.retLen;
    curRetStruct_ = fn.retStruct;
    pushScope();
    for (auto& p : fn.params) {
        if (p.type == Type::Void)
            error(p.line, "参数 '" + p.name + "' 不能是 void 类型");
        if (p.len == -2)
            error(p.line, "列表不能作参数：用切片 T[] 借用（可读写元素/排序），增删用全局列表");
        if (!declare(p.name, {p.type, p.len, p.structName}))
            error(p.line, "参数名重复: " + p.name);
    }
    checkBlock(*fn.body);
    popScope();
    inFn_ = false;
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
            if (ls.declaredLen == -2) {
                // v1 列表 = 全局（与 Scratch"列表在舞台上"同心智）；局部列表牵涉
                // 作用域释放/所有权转移，后续版本再放开
                if (inFn_) error(ls.line, "列表 T[*] 目前只能声明为全局变量（函数外）");
                if (ls.init) error(ls.line, "列表从空开始，用 push 添加元素（不支持初始化字面量）");
                declare(ls.name, {ls.declared, -2, ls.structName});
                if (ls.declared == Type::Struct && !structs_.count(ls.structName))
                    error(ls.line, "未定义的结构体: " + ls.structName);
                if (ls.declared == Type::Unknown || ls.declared == Type::Void)
                    error(ls.line, "列表必须标注元素类型（如 let xs: int[*]）");
                break;
            }
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
                    if (initLen == -2) {   // let y = xs 会造成列表别名（C 里指针共享）
                        errorAt(*ls.init, "列表不能整体赋值给新变量（用切片借用或逐元素访问）");
                        ls.declaredLen = 0;
                    }
                } else if (initT != Type::Unknown) {
                    bool ok = (initT == ls.declared) && (ls.declaredLen == initLen) &&
                              (ls.declared != Type::Struct || ls.init->structName == ls.structName);
                    if (!ok)
                        errorAt(*ls.init, "类型不匹配: 'let " + ls.name + ": " +
                                           declTypeStr(ls.declared, ls.declaredLen, ls.structName) +
                                           "' 不能用 " +
                                           declTypeStr(initT, initLen, ls.init->structName) + " 初始化");
                }
            } else if (ls.declared == Type::Unknown) {
                error(ls.line, "无初始化的 'let " + ls.name + "' 必须标注类型");
            }
            if (ls.declared == Type::Struct && !structs_.count(ls.structName))
                error(ls.line, "未定义的结构体: " + ls.structName);
            if (ls.declaredLen == -1 && scopes_.size() <= 1)
                error(ls.line, "全局变量 '" + ls.name + "' 不能是切片 T[]（切片只能作参数或局部借用）");
            if (!declare(ls.name, {ls.declared, ls.declaredLen, ls.structName}))
                error(ls.line, "变量重复定义: " + ls.name);
            break;
        }
        case StmtKind::Assign: {
            auto& as = static_cast<AssignStmt&>(s);
            VarType vt = lookup(as.name);
            if (vt.base == Type::Unknown)
                errorAt(as, "赋值给未声明的变量: " + as.name);
            if (as.index && !as.field.empty()) {
                // 元素字段赋值 name[idx].field = value（结构体数组/列表的成员直接改）
                if (vt.len == 0 && vt.base != Type::Unknown)
                    error(as.line, as.name + " 不是数组，不能用下标赋值");
                else if (vt.base != Type::Struct && vt.base != Type::Unknown)
                    error(as.line, as.name + " 的元素不是结构体，不能用 '." + as.field + "' 赋值");
                Type it = checkExpr(*as.index);
                if (it != Type::Int && it != Type::Unknown)
                    error(as.line, "数组下标必须是 int，而非 " + std::string(typeName(it)));
                Type ft = Type::Unknown; int fLen = 0; std::string fStruct;
                bool found = false;
                auto sit = structs_.find(vt.structName);
                if (sit != structs_.end())
                    for (auto& d : sit->second) if (d.name == as.field) {
                        ft = d.type; fLen = d.len; fStruct = d.structName; found = true;
                    }
                if (!found && vt.base == Type::Struct)
                    error(as.line, "结构体 " + vt.structName + " 没有字段 '" + as.field + "'");
                Type valT = checkExpr(*as.value);
                bool ok = (valT == ft) && (as.value->arrayLen == fLen) &&
                          (ft != Type::Struct || as.value->structName == fStruct);
                if (valT != Type::Unknown && ft != Type::Unknown && !ok)
                    error(as.line, "字段 '" + as.field + "' 类型不匹配: 应为 " +
                                       declTypeStr(ft, fLen, fStruct) + "，得到 " +
                                       declTypeStr(valT, as.value->arrayLen, as.value->structName));
            } else if (!as.field.empty()) {
                // 字段赋值 name.field = value
                Type ft = Type::Unknown; int fLen = 0; std::string fStruct;
                if (vt.base != Type::Struct && vt.base != Type::Unknown) {
                    error(as.line, as.name + " 不是结构体，不能用 '." + as.field + "' 赋值");
                } else {
                    bool found = false;
                    auto sit = structs_.find(vt.structName);
                    if (sit != structs_.end())
                        for (auto& d : sit->second) if (d.name == as.field) {
                            ft = d.type; fLen = d.len; fStruct = d.structName; found = true;
                        }
                    if (!found && vt.base == Type::Struct)
                        error(as.line, "结构体 " + vt.structName + " 没有字段 '" + as.field + "'");
                }
                Type valT = checkExpr(*as.value);
                bool ok = (valT == ft) && (as.value->arrayLen == fLen) &&
                          (ft != Type::Struct || as.value->structName == fStruct);
                if (valT != Type::Unknown && ft != Type::Unknown && !ok)
                    error(as.line, "字段 '" + as.field + "' 类型不匹配: 应为 " +
                                       declTypeStr(ft, fLen, fStruct) + "，得到 " +
                                       declTypeStr(valT, as.value->arrayLen, as.value->structName));
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
                else if (valT != Type::Unknown && vt.base != Type::Unknown &&
                         (valT != vt.base ||
                          (vt.base == Type::Struct && as.value->structName != vt.structName)))
                    error(as.line, "元素类型不匹配: " + as.name + " 的元素是 " +
                                       declTypeStr(vt.base, 0, vt.structName) + "，却赋以 " +
                                       declTypeStr(valT, 0, as.value->structName));
            } else {
                // 列表不能整体赋值：C 里是指针别名（之后 realloc 会让另一份悬空）。
                // 要复制内容：clear + 逐个 push；要共享：本来就是同一个全局。
                if (vt.len == -2) {
                    errorAt(as, "列表不能整体赋值（要复制内容请 clear 后逐个 push）");
                    checkExpr(*as.value);
                    break;
                }
                Type valT = checkExpr(*as.value);
                int valLen = as.value->arrayLen;
                if (valLen == -2)
                    errorAt(*as.value, "列表不能整体赋给别的变量");
                bool ok = (valT == vt.base) && (valLen == vt.len) &&
                          (vt.base != Type::Struct || as.value->structName == vt.structName);
                if (vt.base != Type::Unknown && valT != Type::Unknown && valLen != -2 && !ok)
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
            loopDepth_++;
            checkBlock(*ws.body);
            loopDepth_--;
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
            loopDepth_++;
            checkBlock(*fs.body);
            loopDepth_--;
            popScope();
            break;
        }
        case StmtKind::Return: {
            auto& rs = static_cast<ReturnStmt&>(s);
            if (rs.value) {
                Type vt = checkExpr(*rs.value);
                if (curRet_ == Type::Void)
                    error(rs.line, "void 函数不能返回值");
                else if (vt != Type::Unknown &&
                         (vt != curRet_ || rs.value->arrayLen != curRetLen_ ||
                          (curRet_ == Type::Struct && rs.value->structName != curRetStruct_)))
                    error(rs.line, "返回类型不匹配: 期望 " +
                                       declTypeStr(curRet_, curRetLen_, curRetStruct_) + "，得到 " +
                                       declTypeStr(vt, rs.value->arrayLen, rs.value->structName));
            } else if (curRet_ != Type::Void) {
                error(rs.line, "非 void 函数必须返回 " +
                                   std::string(typeName(curRet_)) + " 值");
            }
            break;
        }
        case StmtKind::Break:
            if (loopDepth_ == 0) error(s.line, "break 只能用在循环里");
            break;
        case StmtKind::Continue:
            if (loopDepth_ == 0) error(s.line, "continue 只能用在循环里");
            break;
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
                errorAt(v, "使用了未声明的变量: " + v.name);
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
                bool ok = (vt == def->type) && (fi.value->arrayLen == def->len) &&
                          (def->type != Type::Struct || fi.value->structName == def->structName);
                if (vt != Type::Unknown && !ok)
                    error(sl.line, "字段 '" + fi.name + "' 类型应为 " +
                                       declTypeStr(def->type, def->len, def->structName) +
                                       "，得到 " + declTypeStr(vt, fi.value->arrayLen, fi.value->structName));
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
                    fa.type = d.type; fa.arrayLen = d.len; fa.structName = d.structName;
                    return d.type;
                }
            }
            errorAt(fa, "结构体 " + fa.obj->structName + " 没有字段 '" + fa.field + "'");
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
                    errorAt(*b.lhs, "比较运算 '" + op + "' 两侧类型不一致: " +
                                      typeName(lt) + " 与 " + typeName(rt));
                // 字符串支持全部比较：== != 走 strcmp==0，< <= > >= 走 strcmp 符号
                b.type = Type::Bool;
            } else { // + - * / %
                if (lt != Type::Unknown && rt != Type::Unknown && lt != rt)
                    errorAt(*b.lhs, "算术运算 '" + op + "' 两侧类型不一致: " +
                                      typeName(lt) + " 与 " + typeName(rt));
                if (lt == Type::Bool || rt == Type::Bool)
                    error(b.line, "算术运算 '" + op + "' 不能用于 bool");
                if (lt == Type::String || rt == Type::String) {
                    // 字符串仅支持 '+' 拼接（结果仍是 string），不支持 - * / %
                    if (op != "+")
                        error(b.line, "string 只支持 '+' 拼接，不支持 '" + op + "'");
                }
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
            // 内建 len：数组/切片的长度
            if (c.callee == "len") {
                if (c.args.size() != 1) {
                    error(c.line, "len 需要恰好 1 个参数");
                } else {
                    checkExpr(*c.args[0]);
                    if (c.args[0]->arrayLen == 0)
                        error(c.line, "len 只能用于数组、切片或列表");
                }
                c.type = Type::Int;
                return Type::Int;
            }
            // 内建 str：把标量转成字符串（int/float/bool/string → string）
            if (c.callee == "str") {
                if (c.args.size() != 1) {
                    error(c.line, "str 需要恰好 1 个参数");
                } else {
                    Type at = checkExpr(*c.args[0]);
                    if (c.args[0]->arrayLen != 0 || at == Type::Struct || at == Type::Void)
                        error(c.line, "str 只能转换标量 int/float/bool/string");
                }
                c.type = Type::String;
                return Type::String;
            }
            // 内建字符串库（按 UTF-8 码点计数——"你好" 长度是 2 不是 6）
            {
                struct StrFn { const char* name; int argc; Type args[3]; Type ret; };
                static const StrFn STRFNS[] = {
                    {"str_len", 1, {Type::String}, Type::Int},
                    {"str_at", 2, {Type::String, Type::Int}, Type::String},
                    {"str_sub", 3, {Type::String, Type::Int, Type::Int}, Type::String},
                    {"str_find", 2, {Type::String, Type::String}, Type::Int},
                    {"str_contains", 2, {Type::String, Type::String}, Type::Bool},
                    {"str_to_int", 1, {Type::String}, Type::Int},
                    {"str_to_float", 1, {Type::String}, Type::Float},
                };
                for (auto& sf : STRFNS) {
                    if (c.callee != sf.name) continue;
                    if ((int)c.args.size() != sf.argc)
                        error(c.line, std::string(sf.name) + " 需要 " + std::to_string(sf.argc) + " 个参数");
                    for (size_t i = 0; i < c.args.size(); i++) {
                        Type at = checkExpr(*c.args[i]);
                        if ((int)i < sf.argc && at != Type::Unknown &&
                            (at != sf.args[i] || c.args[i]->arrayLen != 0))
                            errorAt(*c.args[i], std::string(sf.name) + " 第 " + std::to_string(i + 1) +
                                                 " 个参数应为 " + typeName(sf.args[i]));
                    }
                    c.type = sf.ret;
                    return c.type;
                }
            }
            // 内建列表操作：push(xs,v) / pop(xs) / insert(xs,i,v) / remove_at(xs,i) / clear(xs)
            // 第一个参数必须是列表（T[*]）变量——列表按值整体拷贝没有意义，增删要打在本体上
            if (c.callee == "push" || c.callee == "pop" || c.callee == "insert" ||
                c.callee == "remove_at" || c.callee == "clear") {
                if (c.args.empty()) { error(c.line, c.callee + " 需要一个列表参数"); c.type = Type::Unknown; return c.type; }
                Type lt = checkExpr(*c.args[0]);
                if (c.args[0]->arrayLen != -2 || c.args[0]->kind != ExprKind::Var) {
                    errorAt(*c.args[0], c.callee + " 的第一个参数必须是列表变量（T[*]）");
                    for (size_t i = 1; i < c.args.size(); i++) checkExpr(*c.args[i]);
                    c.type = Type::Unknown; return c.type;
                }
                const std::string& esn = c.args[0]->structName;
                auto wantElem = [&](Expr& v) {
                    Type vt = checkExpr(v);
                    if (vt != Type::Unknown &&
                        (vt != lt || v.arrayLen != 0 || (lt == Type::Struct && v.structName != esn)))
                        errorAt(v, c.callee + " 的元素类型应为 " +
                                     std::string(lt == Type::Struct ? esn.c_str() : typeName(lt)));
                };
                auto wantInt = [&](Expr& v) {
                    Type vt = checkExpr(v);
                    if (vt != Type::Int && vt != Type::Unknown) errorAt(v, "下标必须是 int");
                };
                if (c.callee == "push") {
                    if (c.args.size() != 2) error(c.line, "push(列表, 值) 需要 2 个参数");
                    else wantElem(*c.args[1]);
                    c.type = Type::Void;
                } else if (c.callee == "pop") {
                    if (c.args.size() != 1) error(c.line, "pop(列表) 需要 1 个参数");
                    c.type = lt; c.structName = esn;      // 返回末元素
                } else if (c.callee == "insert") {
                    if (c.args.size() != 3) error(c.line, "insert(列表, 下标, 值) 需要 3 个参数");
                    else { wantInt(*c.args[1]); wantElem(*c.args[2]); }
                    c.type = Type::Void;
                } else if (c.callee == "remove_at") {
                    if (c.args.size() != 2) error(c.line, "remove_at(列表, 下标) 需要 2 个参数");
                    else wantInt(*c.args[1]);
                    c.type = Type::Void;
                } else {                                   // clear
                    if (c.args.size() != 1) error(c.line, "clear(列表) 需要 1 个参数");
                    c.type = Type::Void;
                }
                return c.type;
            }
            if (checkGenericCall(c)) return c.type;   // 泛型：推断实参类型并改写为实例
            auto it = fns_.find(c.callee);
            if (it == fns_.end()) {
                errorAt(c, "调用了未定义的函数: " + c.callee);
                for (auto& a : c.args) checkExpr(*a);
                c.type = Type::Unknown;
                return Type::Unknown;
            }
            const FnSig& sig = it->second;
            if (c.args.size() != sig.params.size())
                errorAt(c, "函数 " + c.callee + " 期望 " +
                                  std::to_string(sig.params.size()) + " 个参数，得到 " +
                                  std::to_string(c.args.size()));
            for (size_t i = 0; i < c.args.size(); i++) {
                Type at = checkExpr(*c.args[i]);
                if (i < sig.params.size()) {
                    const VarType& pt = sig.params[i];
                    int aLen = c.args[i]->arrayLen;
                    // 切片形参：定长数组 T[N] 与切片 T[] 都可传入（前者自动借用为视图）
                    bool lenOk = (aLen == pt.len) ||
                                 (pt.len == -1 && (aLen > 0 || aLen == -1 || aLen == -2));
                    bool ok = (at == pt.base) && lenOk &&
                              (pt.base != Type::Struct || c.args[i]->structName == pt.structName);
                    // 定长数组借用为切片时必须是可取地址的左值（变量/字段/元素）
                    if (ok && pt.len == -1 && aLen > 0) {
                        ExprKind k = c.args[i]->kind;
                        if (k != ExprKind::Var && k != ExprKind::Field && k != ExprKind::Index)
                            error(c.line, "传给切片参数的数组必须是变量（不能是临时值）");
                    }
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

// 泛型调用：由实参类型反推类型参数，登记实例化请求，并把调用点改写成实例名。
// 形参写作 T / T[] / T[N] 时，用实参的 (元素类型, 结构体名) 绑定 T。
bool TypeChecker::checkGenericCall(Call& c) {
    auto git = generics_.find(c.callee);
    if (git == generics_.end()) return false;
    const FnDecl& gen = *git->second;

    std::map<std::string, TypeArg> subst;
    auto isTypeParam = [&](const std::string& n) {
        for (auto& tp : gen.typeParams) if (tp == n) return true;
        return false;
    };

    for (size_t i = 0; i < c.args.size(); i++) {
        Type at = checkExpr(*c.args[i]);
        if (i >= gen.params.size()) continue;
        const Param& p = gen.params[i];
        if (p.type != Type::Struct || !isTypeParam(p.structName)) continue;   // 非类型变量
        TypeArg ta;
        ta.base = at;
        ta.structName = c.args[i]->structName;
        auto ins = subst.emplace(p.structName, ta);
        if (!ins.second) {                                    // 同一类型参数出现多次：须一致
            const TypeArg& prev = ins.first->second;
            if (prev.base != ta.base || prev.structName != ta.structName)
                error(c.line, "泛型参数 " + p.structName + " 推断冲突: " +
                                  typeName(prev.base) + " 与 " + typeName(ta.base));
        }
    }
    if (c.args.size() != gen.params.size())
        error(c.line, "函数 " + c.callee + " 期望 " + std::to_string(gen.params.size()) +
                          " 个参数，得到 " + std::to_string(c.args.size()));
    bool inferred = true;
    for (auto& tp : gen.typeParams)
        if (!subst.count(tp)) {
            error(c.line, "无法从实参推断泛型参数 " + tp + "（请让它出现在某个参数类型里）");
            inferred = false;
        }
    if (!inferred) {                       // 推断失败：不要实例化，否则会再报一串派生错误
        c.type = Type::Unknown;
        return true;
    }

    std::string mangled = mangleName(gen.name, gen.typeParams, subst);
    if (!instSeen_.count(mangled)) {                          // 同一实例只生成一次
        instSeen_[mangled] = true;
        insts_.push_back({ &gen, subst, mangled });
    }
    c.resolved = mangled;                                     // 代码生成时指向具体实例

    // 返回类型：把类型变量换成实参
    Type rt = gen.ret; std::string rs = gen.retStruct;
    if (rt == Type::Struct && isTypeParam(rs)) {
        auto it = subst.find(rs);
        if (it != subst.end()) { rt = it->second.base; rs = it->second.structName; }
    }
    c.type = rt; c.structName = rs; c.arrayLen = gen.retLen;
    return true;
}

} // namespace sincoding
