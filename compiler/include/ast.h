// ast.h — 抽象语法树（唯一真相源 Single Source of Truth）
//
// 设计原则：积木是 AST 的可视化渲染，文本是 AST 的序列化。
// 所有节点都带 line 信息，方便编辑器定位错误与回填积木。
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace sincoding {

// 语言的类型系统（初期：静态类型，显式 + 局部推断）
enum class Type { Unknown, Int, Float, Bool, String, Void };

const char* typeName(Type t);
const char* typeToC(Type t);

// ---------- 表达式 ----------
enum class ExprKind { IntLit, FloatLit, BoolLit, StringLit, Var, Unary, Binary, Call, Index, ArrayLit };

struct Expr {
    ExprKind kind;
    Type type = Type::Unknown; // 元素/标量类型，由类型检查阶段填充
    int arrayLen = 0;          // >0 表示该表达式是定长数组（type 为元素类型）
    int line = 0;
    virtual ~Expr() = default;
protected:
    explicit Expr(ExprKind k) : kind(k) {}
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLit : Expr {
    long long value;
    IntLit() : Expr(ExprKind::IntLit) {}
};

struct FloatLit : Expr {
    double value;
    FloatLit() : Expr(ExprKind::FloatLit) {}
};

struct BoolLit : Expr {
    bool value;
    BoolLit() : Expr(ExprKind::BoolLit) {}
};

struct StringLit : Expr {
    std::string value; // 已解码（不含外层引号、转义已处理）
    StringLit() : Expr(ExprKind::StringLit) {}
};

struct Var : Expr {
    std::string name;
    Var() : Expr(ExprKind::Var) {}
};

struct Unary : Expr {
    std::string op;   // "-" 或 "!"
    ExprPtr operand;
    Unary() : Expr(ExprKind::Unary) {}
};

struct Binary : Expr {
    std::string op;   // + - * / % == != < <= > >= && ||
    ExprPtr lhs, rhs;
    Binary() : Expr(ExprKind::Binary) {}
};

struct Call : Expr {
    std::string callee;
    std::vector<ExprPtr> args;
    Call() : Expr(ExprKind::Call) {}
};

struct IndexExpr : Expr {     // arr[idx]
    ExprPtr arr;
    ExprPtr idx;
    IndexExpr() : Expr(ExprKind::Index) {}
};

struct ArrayLit : Expr {      // [e1, e2, ...]，仅用于 let 初始化
    std::vector<ExprPtr> elems;
    ArrayLit() : Expr(ExprKind::ArrayLit) {}
};

// ---------- 语句 ----------
enum class StmtKind { Let, Assign, If, While, Return, ExprStmt, Block };

struct Stmt {
    StmtKind kind;
    int line = 0;
    virtual ~Stmt() = default;
protected:
    explicit Stmt(StmtKind k) : kind(k) {}
};
using StmtPtr = std::unique_ptr<Stmt>;

struct Block : Stmt {
    std::vector<StmtPtr> stmts;
    Block() : Stmt(StmtKind::Block) {}
};
using BlockPtr = std::unique_ptr<Block>;

struct LetStmt : Stmt {
    std::string name;
    Type declared = Type::Unknown; // Unknown 表示需要推断
    int declaredLen = 0;           // >0 表示定长数组
    ExprPtr init;                  // 可为空（有类型标注时零初始化）
    LetStmt() : Stmt(StmtKind::Let) {}
};

struct AssignStmt : Stmt {
    std::string name;
    ExprPtr index;   // 非空表示元素赋值 name[index] = value
    ExprPtr value;
    AssignStmt() : Stmt(StmtKind::Assign) {}
};

struct IfStmt : Stmt {
    ExprPtr cond;
    BlockPtr thenBlock;          // then 分支
    BlockPtr elseBlock;          // 可为空
    IfStmt() : Stmt(StmtKind::If) {}
};

struct WhileStmt : Stmt {
    ExprPtr cond;
    BlockPtr body;
    WhileStmt() : Stmt(StmtKind::While) {}
};

struct ReturnStmt : Stmt {
    ExprPtr value; // 可为空（void 返回）
    ReturnStmt() : Stmt(StmtKind::Return) {}
};

struct ExprStmt : Stmt {
    ExprPtr expr;
    ExprStmt() : Stmt(StmtKind::ExprStmt) {}
};

// ---------- 顶层 ----------
struct Param {
    std::string name;
    Type type;
    int line;
};

struct FnDecl {
    std::string name;
    std::vector<Param> params;
    Type ret = Type::Void;
    BlockPtr body;          // extern 函数为空（无函数体）
    bool isExtern = false;  // 由 'extern fn' 声明，链接到外部/运行时实现
    int line = 0;
};
using FnPtr = std::unique_ptr<FnDecl>;

struct Program {
    std::vector<FnPtr> fns;
};

} // namespace sincoding
