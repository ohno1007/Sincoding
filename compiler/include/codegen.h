// codegen.h — AST → C 代码生成
#pragma once
#include "ast.h"
#include <sstream>
#include <string>

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
    void indent();

    std::ostringstream out_;
    int depth_ = 0;
};

} // namespace sincoding
