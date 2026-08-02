// type_checker.h — 静态类型检查 + 局部类型推断
#pragma once
#include "ast.h"
#include "lexer.h" // Diagnostic
#include <string>
#include <unordered_map>
#include <vector>

namespace sincoding {

// 变量/类型描述：标量 {base,0}，定长数组 {base,len>0}，结构体 {Struct,0,name}
struct VarType {
    Type base = Type::Unknown;
    int len = 0;
    std::string structName;
};

struct FnSig {
    std::vector<VarType> params;
    VarType ret;
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
    std::unordered_map<std::string, std::vector<StructField>> structs_; // 结构体定义
    std::vector<std::unordered_map<std::string, VarType>> scopes_;
    Type curRet_ = Type::Void;
    std::string curRetStruct_;
    std::vector<Diagnostic> errors_;
};

} // namespace sincoding
