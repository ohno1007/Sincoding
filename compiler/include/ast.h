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
enum class Type { Unknown, Int, Float, Bool, String, Struct, Void };

const char* typeName(Type t);
const char* typeToC(Type t);

// ---------- 表达式 ----------
enum class ExprKind { IntLit, FloatLit, BoolLit, StringLit, Var, Unary, Binary,
                      Call, Index, ArrayLit, Field, StructLit };

struct Expr {
    ExprKind kind;
    Type type = Type::Unknown;  // 元素/标量类型，由类型检查阶段填充
    int arrayLen = 0;           // >0 定长数组，-1 切片 T[]（type 为元素类型）
    std::string structName;     // type==Struct 时的结构体名
    int line = 0;
    int col = 0;                // 标识符起始列（IDE 定位用；与 token.col 同基数）
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
    std::string callee;      // 用户写的名字（唯一真相：序列化/IDE 查询都用它）
    // 泛型调用经单态化解析出的具体实例名（如 total__int）。仅代码生成使用，
    // 不参与序列化——否则会把 total(a) 写回成 total__int(a)，破坏往返幂等。
    std::string resolved;
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

struct FieldAccess : Expr {   // obj.field
    ExprPtr obj;
    std::string field;
    FieldAccess() : Expr(ExprKind::Field) {}
};

struct FieldInit { std::string name; ExprPtr value; };

struct StructLit : Expr {     // Name { f1: e1, f2: e2 }
    std::string typeName;
    std::vector<FieldInit> fields;
    StructLit() : Expr(ExprKind::StructLit) {}
};

// ---------- 语句 ----------
enum class StmtKind { Let, Assign, If, While, For, Return, ExprStmt, Block };

struct Stmt {
    StmtKind kind;
    int line = 0;
    int col = 0;                // 主标识符起始列（let/assign/for 的变量名，IDE 定位用）
    std::string module;         // 非空 = 来自 import 的模块（序列化时跳过，只写 import 行）
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
    std::string structName;        // declared==Struct 时的结构体名
    ExprPtr init;                  // 可为空（有类型标注时零初始化）
    LetStmt() : Stmt(StmtKind::Let) {}
};

struct AssignStmt : Stmt {
    std::string name;
    ExprPtr index;          // 非空表示元素赋值 name[index] = value
    std::string field;      // 非空表示字段赋值 name.field = value
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

struct ForStmt : Stmt {       // for v in start..end { body }
    std::string var;
    ExprPtr start;
    ExprPtr end;
    BlockPtr body;
    ForStmt() : Stmt(StmtKind::For) {}
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
    int len = 0;            // >0 定长数组（值语义），-1 切片 T[]（借用视图）
    std::string structName; // type==Struct 时
    int line;
    int col = 0;            // 参数名起始列
};

struct FnDecl {
    std::string name;
    // 泛型类型参数 fn f<T>(...)：非空表示这是泛型模板，由单态化按调用点实例化。
    // 参数/返回/局部里以「结构体名 == 某个类型参数名」的形式引用它们。
    std::vector<std::string> typeParams;
    std::vector<Param> params;
    Type ret = Type::Void;
    int retLen = 0;         // >0 表示返回定长数组 T[N]
    std::string retStruct;  // ret==Struct 时
    BlockPtr body;          // extern 函数为空（无函数体）
    bool isExtern = false;  // 由 'extern fn' 声明，链接到外部/运行时实现
    int line = 0;
    int col = 0;            // 函数名起始列
    std::string module;     // 非空 = 来自 import 的模块
};
using FnPtr = std::unique_ptr<FnDecl>;

// 结构体声明：struct Name { field: type, ... }
// 字段可为标量、已声明的结构体（type==Struct + structName）、或标量定长数组（len>0）。
struct StructField {
    std::string name;
    Type type;
    int len = 0;             // >0 表示数组字段 T[N]（元素为标量）
    std::string structName;  // type==Struct 时的结构体名
    int line;
    int col = 0;             // 字段名起始列
};
struct StructDecl {
    std::string name;
    std::vector<StructField> fields;
    int line = 0;
    int col = 0;             // 结构体名起始列
    std::string module;      // 非空 = 来自 import 的模块
};
using StructPtr = std::unique_ptr<StructDecl>;

// import "std/mathx" —— 模块导入（标准库随编译器内置，用户库从磁盘解析）
struct ImportDecl {
    std::string name;
    int line = 0;
    int col = 0;
};

struct Program {
    std::vector<ImportDecl> imports; // 导入声明（序列化时原样写回）
    std::vector<StructPtr> structs;  // 结构体声明
    std::vector<StmtPtr> globals;    // 顶层全局变量（LetStmt）
    std::vector<FnPtr> fns;
};

} // namespace sincoding
