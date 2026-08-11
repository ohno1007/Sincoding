// modules.cpp — import 模块解析实现（详见 modules.h）
#include "modules.h"
#include "parser.h"
#include "std_modules.h"   // 构建期生成：内置标准库源码表

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace sincoding {
namespace {

// 内置标准库查表
bool builtinModule(const std::string& name, std::string& out) {
    for (const StdModule* m = SIN_STD_MODULES; m->name; ++m) {
        if (name == m->name) { out = m->src; return true; }
    }
    return false;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}

// 按解析顺序查找模块源码；成功时填 src 并返回其所在目录（供嵌套导入用）
bool findModule(const std::string& name, const std::string& baseDir,
                std::string& src, std::string& dir) {
    if (builtinModule(name, src)) { dir = ""; return true; }   // 内置：无目录

    auto tryPath = [&](const std::string& d) {
        if (d.empty()) return false;
        std::string p = d + "/" + name + ".sin";
        if (!readFile(p, src)) return false;
        auto slash = p.find_last_of('/');
        dir = (slash == std::string::npos) ? "" : p.substr(0, slash);
        return true;
    };
    if (tryPath(baseDir)) return true;

    if (const char* env = std::getenv("SINCODING_PATH")) {
        std::string paths(env), one;
        std::istringstream ss(paths);
        while (std::getline(ss, one, ':')) if (tryPath(one)) return true;
    }
    return false;
}

struct Resolver {
    std::vector<Diagnostic>& diags;
    std::unordered_set<std::string> loaded;   // 已合并的模块（去重 + 防环）
    std::unordered_set<std::string> loading;  // 正在解析的（检测循环导入）
    bool ok = true;

    void err(int line, int col, const std::string& msg) {
        diags.push_back({line, col, msg});
        ok = false;
    }

    // 多个模块可能借同一个 libm 函数：重复的 extern 原型跳过即可（与主程序的去重见下方）
    static bool dupExtern(const Program& dst, const FnDecl& fn) {
        if (!fn.isExtern) return false;
        for (auto& f : dst.fns)
            if (f->name == fn.name && f->isExtern) return true;
        return false;
    }

    void merge(Program& dst, Program&& mod, const std::string& name) {
        for (auto& st : mod.structs) { st->module = name; dst.structs.push_back(std::move(st)); }
        for (auto& g  : mod.globals) { g->module  = name; dst.globals.push_back(std::move(g)); }
        for (auto& fn : mod.fns) {
            if (dupExtern(dst, *fn)) continue;
            fn->module = name;
            dst.fns.push_back(std::move(fn));
        }
    }

    void resolveInto(Program& dst, const std::vector<ImportDecl>& imports, const std::string& baseDir) {
        for (const auto& im : imports) {
            if (loaded.count(im.name)) continue;                 // 已导入过：幂等
            if (loading.count(im.name)) {
                err(im.line, im.col, "循环导入: " + im.name);
                continue;
            }
            std::string src, dir;
            if (!findModule(im.name, baseDir, src, dir)) {
                err(im.line, im.col, "找不到模块: " + im.name +
                        "（内置标准库、\"" + baseDir + "\" 或 SINCODING_PATH 下均无 " + im.name + ".sin）");
                continue;
            }
            Lexer lexer(src);
            Parser parser(lexer.tokenize());
            Program mod = parser.parseProgram();
            for (auto& d : lexer.errors())  err(d.line, d.col, "模块 " + im.name + ": " + d.message);
            for (auto& d : parser.errors()) err(d.line, d.col, "模块 " + im.name + ": " + d.message);

            loading.insert(im.name);
            resolveInto(dst, mod.imports, dir);                  // 模块自身的依赖（深度优先）
            loading.erase(im.name);

            loaded.insert(im.name);
            merge(dst, std::move(mod), im.name);
        }
    }
};

} // namespace

bool resolveImports(Program& prog, const std::string& baseDir, std::vector<Diagnostic>& diags) {
    if (prog.imports.empty()) return true;
    Resolver r{diags, {}, {}, true};
    // 先收集到临时程序，再整体前置合并到主程序
    Program mods;
    r.resolveInto(mods, prog.imports, baseDir);

    // 与主程序重复的 extern 原型要去掉：主程序和库都借同一个 libm 函数是常态
    std::vector<FnPtr> fns;
    for (auto& fn : mods.fns) {
        bool dup = false;
        if (fn->isExtern)
            for (auto& own : prog.fns)
                if (own->name == fn->name && own->isExtern) { dup = true; break; }
        if (!dup) fns.push_back(std::move(fn));
    }

    // 整块前置插入（保持模块内声明顺序：结构体字段依赖前置声明，顺序不能颠倒）
    auto mv = [](auto& v) {
        return std::make_pair(std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
    };
    auto s = mv(mods.structs); prog.structs.insert(prog.structs.begin(), s.first, s.second);
    auto g = mv(mods.globals); prog.globals.insert(prog.globals.begin(), g.first, g.second);
    auto f = mv(fns);          prog.fns.insert(prog.fns.begin(), f.first, f.second);
    return r.ok;
}

} // namespace sincoding
