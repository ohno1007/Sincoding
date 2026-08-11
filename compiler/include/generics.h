// generics.h — 泛型单态化（monomorphization）
//
// 泛型函数 `fn sum<T>(xs: T[]) -> T` 不直接生成代码，而是**按调用点的实际类型
// 克隆出一份具体实现**（如 sum__int / sum__float），调用点改指向该实例。
// 于是类型检查与代码生成看到的永远是全具体类型的程序——codegen 完全不用改。
//
// 类型参数的表示：复用「结构体名」槽位——参数/返回/局部里 structName 等于
// 某个类型参数名（如 "T"）即为类型变量，替换时换成实参的 (type, structName)。
#pragma once
#include "ast.h"
#include <map>
#include <string>

namespace sincoding {

// 一个类型实参：标量用 base；结构体用 base=Struct + structName
struct TypeArg {
    Type base = Type::Unknown;
    std::string structName;
};

// 实例名：sum + {T:int} → "sum__int"（结构体实参用结构体名）
std::string mangleName(const std::string& fnName, const std::vector<std::string>& typeParams,
                       const std::map<std::string, TypeArg>& subst);

// 按 subst 克隆泛型函数 gen，产出名为 mangled 的具体实例（typeParams 清空）
FnPtr instantiateFn(const FnDecl& gen, const std::map<std::string, TypeArg>& subst,
                    const std::string& mangled);

// 类型检查 + 泛型单态化的统一入口：
// 反复「检查 → 把发现的泛型调用实例化并追加到 prog → 再检查」，直到不再产生新实例。
// 实例本身可能再调用别的泛型，故需迭代到不动点。返回最终一轮的检查结果。
// 诊断信息经 outDiags 返回（只保留最终一轮，避免中间轮的「实例尚不存在」噪音）。
bool checkWithGenerics(Program& prog, std::vector<struct Diagnostic>& outDiags);

} // namespace sincoding
