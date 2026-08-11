// type_checker.h — 静态类型检查 + 局部类型推断
#pragma once
#include "ast.h"
#include "generics.h"
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
    bool isConst = false;   // const 声明的名字：一切再赋值都被拒绝
};

struct FnSig {
    std::vector<VarType> params;
    VarType ret;
};

// 一次待实例化请求：泛型函数 + 类型实参 + 目标实例名
struct InstReq {
    const FnDecl* gen;
    std::map<std::string, TypeArg> subst;
    std::string mangled;
};

class TypeChecker {
public:
    bool check(Program& prog);
    const std::vector<Diagnostic>& errors() const { return errors_; }
    // 检查过程中发现的泛型调用（调用点已被改写为实例名，由驱动方补出实例）
    const std::vector<InstReq>& instantiations() const { return insts_; }

private:
    void error(int line, const std::string& msg);          // 无精确列时用
    void errorAt(const Expr& e, const std::string& msg);   // 指向出错的表达式
    void errorAt(const Stmt& st, const std::string& msg);  // 指向出错的语句
    void checkFn(FnDecl& fn);
    void checkBlock(Block& block);
    void checkStmt(Stmt& s);
    Type checkExpr(Expr& e);

    // 作用域栈：变量名 -> 类型
    void pushScope() { scopes_.emplace_back(); }
    void popScope() { scopes_.pop_back(); }
    bool declare(const std::string& name, VarType t);
    VarType lookup(const std::string& name) const;

    // 泛型模板（不参与常规签名表；调用点按实参推断后实例化）
    std::unordered_map<std::string, const FnDecl*> generics_;
    std::vector<InstReq> insts_;
    std::unordered_map<std::string, bool> instSeen_;
    bool checkGenericCall(Call& c);   // 命中泛型则推断+改写调用点，返回 true

    std::unordered_map<std::string, FnSig> fns_;
    std::unordered_map<std::string, std::vector<StructField>> structs_; // 结构体定义
    std::vector<std::unordered_map<std::string, VarType>> scopes_;
    bool inFn_ = false;               // 正在检查函数体（列表 v1 只许全局声明）
    int loopDepth_ = 0;               // 循环嵌套深度（break/continue 只许在循环里）
    Type curRet_ = Type::Void;
    int curRetLen_ = 0;          // 当前函数返回类型的数组长度（>0 表示返回数组）
    std::string curRetStruct_;
    std::vector<Diagnostic> errors_;
};

} // namespace sincoding
