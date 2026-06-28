// blockmodel.js — 积木模型工具：模型 ⇄ Sincoding 源码
//
// 积木模型的 schema 与编译器 `sinc --emit blocks` 的输出一致，
// 即它就是 AST 在前端的表示（唯一真相源）。
//
// modelToSource 必须与 C++ serializeSource 逐字节一致：
// 这样「积木编辑 → 改模型 → 写回文本」得到的文本，
// 与规范引擎 `sinc --emit src` 完全对齐。
//
// 同时支持 Node（module.exports）与浏览器（window.BlockModel）。
(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.BlockModel = api;
})(typeof self !== "undefined" ? self : this, function () {

  function fmtFloat(v) {
    let s = String(v);
    // 与 C++ 一致：确保浮点有小数点，避免被当成整型
    if (!/[.eEnN]/.test(s)) s += ".0";
    return s;
  }

  // 表达式 → 文本（与 serializer.cpp 的 writeExpr 对齐：二元/一元全括号）
  function expr(node) {
    switch (node.block) {
      case "int": return String(node.value);
      case "float": return fmtFloat(node.value);
      case "bool": return node.value ? "true" : "false";
      case "string": {
        const s = String(node.value)
          .replace(/\\/g, "\\\\").replace(/"/g, '\\"')
          .replace(/\n/g, "\\n").replace(/\t/g, "\\t").replace(/\r/g, "\\r");
        return '"' + s + '"';
      }
      case "var": return node.name;
      case "unary": return "(" + node.op + expr(node.operand) + ")";
      case "binary": return "(" + expr(node.lhs) + " " + node.op + " " + expr(node.rhs) + ")";
      case "call":
        return node.callee + "(" + node.args.map(expr).join(", ") + ")";
      default: throw new Error("未知表达式积木: " + node.block);
    }
  }

  const pad = (d) => "    ".repeat(d);

  function stmt(node, d) {
    const ind = pad(d);
    switch (node.block) {
      case "let":
        return ind + "let " + node.name + ": " + node.type + " = " + expr(node.value) + "\n";
      case "assign":
        return ind + node.name + " = " + expr(node.value) + "\n";
      case "if": {
        let s = ind + "if " + expr(node.cond) + " " + block(node.then, d);
        if (node.else) s += " else " + block(node.else, d);
        return s + "\n";
      }
      case "while":
        return ind + "while " + expr(node.cond) + " " + block(node.body, d) + "\n";
      case "return":
        return ind + "return" + (node.value ? " " + expr(node.value) : "") + "\n";
      case "expr":
        return ind + expr(node.expr) + "\n";
      case "block_group":
        return ind + block(node.body, d) + "\n";
      default: throw new Error("未知语句积木: " + node.block);
    }
  }

  // 语句列表 → "{ ... }"
  function block(stmts, d) {
    let s = "{\n";
    for (const st of stmts) s += stmt(st, d + 1);
    s += pad(d) + "}";
    return s;
  }

  function fn(f) {
    let s = "";
    if (f.block === "extern_fn") s += "extern ";
    s += "fn " + f.name + "(";
    s += f.params.map((p) => p.name + ": " + p.type).join(", ");
    s += ")";
    if (f.ret && f.ret !== "void") s += " -> " + f.ret;
    if (f.block === "extern_fn") {
      s += "\n";
    } else {
      s += " " + block(f.body, 0) + "\n";
    }
    return s;
  }

  // 完整程序模型 → Sincoding 源码
  function modelToSource(program) {
    const fns = program.program || program; // 容忍直接传 fns 数组
    return fns.map(fn).join("\n");
  }

  return { modelToSource, expr, fmtFloat };
});
