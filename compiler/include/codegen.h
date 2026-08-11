// codegen.h — AST → C 代码生成
#pragma once
#include "ast.h"
#include <sstream>
#include <string>
#include <unordered_map>

namespace sincoding {

class CodeGen {
public:
    // debug=true 时注入调试钩子（行号 + 变量监视），供原生成品的 F12 调试面板使用。
    // 发布构建不开此开关，生成的 C 与原先逐字节一致，零开销。
    explicit CodeGen(bool debug = false) : debug_(debug) {}

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
    void emitDbgLine(const Stmt& s);                        // --debug: 上报当前行
    void emitDbgVar(const std::string& n, Type t, int len); // --debug: 上报标量变量

    bool debug_ = false;
    std::ostringstream out_;
    int depth_ = 0;
    std::unordered_map<std::string, const FnDecl*> fns_;  // 调用点查形参类型
    std::unordered_map<std::string, std::pair<Type,int>> varTypes_;  // 变量 → (类型,长度)用
};

} // namespace sincoding
