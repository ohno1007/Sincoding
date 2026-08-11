// codegen.h — AST → C 代码生成
#pragma once
#include "ast.h"
#include <sstream>
#include <string>
#include <unordered_map>

namespace sincoding {

class CodeGen {
public:
    // 生成完整的、可被 gcc/clang/emcc 编译的 C 源文件文本。
    std::string generate(const Program& prog);

private:
    void emitFnProto(const FnDecl& fn);
    void emitFn(const FnDecl& fn);
    void emitBlock(const Block& block);
    void emitStmt(const Stmt& s);
    void emitExpr(const Expr& e);
    void emitPrint(const Call& c);
    void emitStr(const Call& c);   // 内建 str(x)：标量 → 字符串
    void emitLen(const Call& c);   // 内建 len(x)：数组/切片长度
    void emitArg(const Expr& a, const Param& p);  // 实参（定长数组传给切片时自动借用）
    void indent();

    std::ostringstream out_;
    int depth_ = 0;
    std::unordered_map<std::string, const FnDecl*> fns_;  // 调用点查形参类型用
};

} // namespace sincoding
