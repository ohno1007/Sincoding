// comments.cpp — 把词法收集的行注释挂回 AST（注释保真）
//
// 规则（确定性 + 幂等，保证「源码 → AST → 源码」注释不丢、位置稳定）：
//   1. 行尾注释：注释与某节点同行且列在其后 → 挂为该行**最后一个**起始节点的 tailComment
//   2. 整行注释：挂为源码顺序中**下一个**节点的 preComments（保持出现顺序）
//   3. 最后一个节点之后的孤立注释 → Program.tailComments
// 节点锚点包括：import / 结构体 / 结构体字段 / 全局变量 / 函数 / 函数体内所有语句（递归）。
#include "parser.h"

#include <algorithm>
#include <vector>

namespace sincoding {
namespace {

// 一个可挂注释的锚点：指向节点的 preComments / tailComment 存储
struct Anchor {
    int line;
    int col;
    std::vector<std::string>* pre;
    std::string* tail;
};

void anchorsInStmt(Stmt& s, std::vector<Anchor>& out);

void anchorsInBlock(Block& b, std::vector<Anchor>& out) {
    for (auto& sp : b.stmts) anchorsInStmt(*sp, out);
}

void anchorsInStmt(Stmt& s, std::vector<Anchor>& out) {
    out.push_back({s.line, s.col, &s.preComments, &s.tailComment});
    switch (s.kind) {
        case StmtKind::If: {
            auto& i = static_cast<IfStmt&>(s);
            anchorsInBlock(*i.thenBlock, out);
            if (i.elseBlock) anchorsInBlock(*i.elseBlock, out);
            break;
        }
        case StmtKind::While: anchorsInBlock(*static_cast<WhileStmt&>(s).body, out); break;
        case StmtKind::For: anchorsInBlock(*static_cast<ForStmt&>(s).body, out); break;
        case StmtKind::Block: anchorsInBlock(static_cast<Block&>(s), out); break;
        default: break;
    }
}

} // namespace

void attachComments(Program& prog, const std::vector<CommentTok>& comments) {
    if (comments.empty()) return;
    std::vector<Anchor> anchors;
    for (auto& im : prog.imports)
        anchors.push_back({im.line, im.col, &im.preComments, &im.tailComment});
    for (auto& st : prog.structs) {
        if (!st->module.empty()) continue;
        anchors.push_back({st->line, st->col, &st->preComments, &st->tailComment});
        for (auto& f : st->fields)
            anchors.push_back({f.line, f.col, &f.preComments, &f.tailComment});
    }
    for (auto& g : prog.globals) if (g->module.empty()) anchorsInStmt(*g, anchors);
    for (auto& fn : prog.fns) {
        if (!fn->module.empty()) continue;
        anchors.push_back({fn->line, fn->col, &fn->preComments, &fn->tailComment});
        if (fn->body) anchorsInBlock(*fn->body, anchors);
    }
    // 按 (line, col) 排序：同一行取列最靠后的做行尾锚点
    std::stable_sort(anchors.begin(), anchors.end(), [](const Anchor& a, const Anchor& b) {
        return a.line != b.line ? a.line < b.line : a.col < b.col;
    });

    for (const auto& c : comments) {
        // 规则 1：行尾注释 → 同行最后一个起始于注释之前的锚点
        Anchor* tailHit = nullptr;
        for (auto& a : anchors)
            if (a.line == c.line && a.col < c.col) tailHit = &a;
        if (tailHit) {
            if (!tailHit->tail->empty()) *tailHit->tail += " ";
            *tailHit->tail += c.text;
            continue;
        }
        // 规则 2：整行注释 → 下一个锚点的 preComments
        Anchor* next = nullptr;
        for (auto& a : anchors)
            if (a.line > c.line) { next = &a; break; }
        if (next) next->pre->push_back(c.text);
        else prog.tailComments.push_back(c.text);   // 规则 3：文件尾孤立注释
    }
}

} // namespace sincoding
