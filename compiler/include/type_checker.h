// type_checker.h — 静态类型检查 + 局部类型推断
#pragma once
#include "ast.h"
#include "lexer.h" // Diagnostic
#include <string>
#include <unordered_map>
#include <vector>

namespace sincoding {

struct FnSig {
    std::vector<Type> params;
    Type ret;
};

// 变量类型：标量为 {base, 0}，定长数组为 {base, len>0}
struct VarType {
    Type base = Type::Unknown;
    int len = 0;
};

class TypeChecker {
public:
    bool check(Program& prog);
    const std::vector<Diagnostic>& errors() const { return errors_; }

private:
    void error(int line, const std::string& msg);
    void checkFn(FnDecl& fn);
    void checkBlock(Block& block);
    void checkStmt(Stmt& s);
    Type checkExpr(Expr& e);

    // 作用域栈：变量名 -> 类型
    void pushScope() { scopes_.emplace_back(); }
    void popScope() { scopes_.pop_back(); }
    bool declare(const std::string& name, VarType t);
    VarType lookup(const std::string& name) const;

    std::unordered_map<std::string, FnSig> fns_;
    std::vector<std::unordered_map<std::string, VarType>> scopes_;
    Type curRet_ = Type::Void;
    std::vector<Diagnostic> errors_;
};

} // namespace sincoding
