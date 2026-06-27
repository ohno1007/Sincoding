// app.js — 积木 IDE：多精灵 / 多页积木 + 无限画布 + 可编辑积木 + 实时文本写回
//
// 模型（积木树）就是前端的 AST，是唯一真相源。每个精灵拥有自己的一页积木
// （program）与一组造型（costumes）。切换精灵 = 切换积木页 + 造型集。
//   编辑积木 → 改模型 → BlockModel.modelToSource → 文本视图
(function () {
  "use strict";

  // ---------------- 项目 / 精灵 ----------------
  function sampleMain() {
    return { block: "fn", name: "main", params: [], ret: "int",
      body: [{ block: "return", value: { block: "int", value: 0 } }] };
  }
  function sampleUpdate() {
    return { block: "fn", name: "update", params: [], ret: "void",
      body: [{ block: "expr", expr: { block: "call", callee: "print",
        args: [{ block: "int", value: 1 }] } }] };
  }
  function placeFns(program) {
    program.forEach((fn, i) => {
      if (fn._x === undefined) { fn._x = 40 + (i % 2) * 360; fn._y = 36 + i * 300; }
    });
  }
  function defaultProject() {
    const p1 = (window.SIN_BLOCKS && window.SIN_BLOCKS.program)
      ? window.SIN_BLOCKS.program : [sampleMain()];
    const sprites = [
      { name: "精灵1", icon: "🐱", program: p1, costumes: [] },
      { name: "精灵2", icon: "🎈", program: [sampleUpdate()], costumes: [] },
    ];
    sprites.forEach((s) => placeFns(s.program));
    return { sprites, cur: 0 };
  }

  const project = window.SIN_PROJECT || defaultProject();
  const sprite = () => project.sprites[project.cur];
  let selected = sprite().program[0];
  let ce = null; // 造型编辑器

  // ---------------- 视图变换（无限画布） ----------------
  const wrap = document.getElementById("canvas-wrap");
  const canvas = document.getElementById("canvas");
  const view = { x: 0, y: 0, k: 1 };

  function applyView() {
    canvas.style.transform = `translate(${view.x}px, ${view.y}px) scale(${view.k})`;
    const zt = document.querySelector("#zoom-hint .zt");
    if (zt) zt.textContent = `缩放 ${Math.round(view.k * 100)}%　·　拖空白处平移，滚轮缩放`;
  }

  let panning = null;
  wrap.addEventListener("pointerdown", (e) => {
    if (e.target !== wrap && e.target !== canvas) return;
    panning = { sx: e.clientX, sy: e.clientY, ox: view.x, oy: view.y };
    wrap.classList.add("panning");
  });
  window.addEventListener("pointermove", (e) => {
    if (!panning) return;
    view.x = panning.ox + (e.clientX - panning.sx);
    view.y = panning.oy + (e.clientY - panning.sy);
    applyView();
  });
  window.addEventListener("pointerup", () => { panning = null; wrap.classList.remove("panning"); });

  wrap.addEventListener("wheel", (e) => {
    e.preventDefault();
    const r = wrap.getBoundingClientRect();
    const mx = e.clientX - r.left, my = e.clientY - r.top;
    const factor = e.deltaY < 0 ? 1.1 : 1 / 1.1;
    const nk = Math.min(2.5, Math.max(0.3, view.k * factor));
    view.x = mx - (mx - view.x) * (nk / view.k);
    view.y = my - (my - view.y) * (nk / view.k);
    view.k = nk;
    applyView();
  }, { passive: false });

  // ---------------- 文本写回 ----------------
  const textOut = document.getElementById("text-out");
  function refreshText() {
    try { textOut.textContent = window.BlockModel.modelToSource(sprite().program); }
    catch (err) { textOut.textContent = "// 序列化错误: " + err.message; }
  }

  // ---------------- DOM 工具 ----------------
  function el(tag, cls, txt) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (txt !== undefined) e.textContent = txt;
    return e;
  }
  function field(getStr, setStr, cls) {
    const f = el("span", "field" + (cls ? " " + cls : ""), getStr());
    f.contentEditable = "true"; f.spellcheck = false;
    f.addEventListener("input", () => { setStr(f.textContent); refreshText(); });
    f.addEventListener("pointerdown", (e) => e.stopPropagation());
    f.addEventListener("keydown", (e) => { if (e.key === "Enter") { e.preventDefault(); f.blur(); } });
    return f;
  }

  // ---------------- 积木渲染 ----------------
  function renderExpr(node) {
    switch (node.block) {
      case "int":
        return field(() => String(node.value),
          (s) => { const n = parseInt(s, 10); node.value = isNaN(n) ? 0 : n; }, "lit");
      case "float":
        return field(() => String(node.value),
          (s) => { const n = parseFloat(s); node.value = isNaN(n) ? 0 : n; }, "lit");
      case "bool": {
        const p = el("span", "pill lit", node.value ? "true" : "false");
        p.style.cursor = "pointer";
        p.addEventListener("pointerdown", (e) => e.stopPropagation());
        p.addEventListener("click", () => { node.value = !node.value; p.textContent = node.value ? "true" : "false"; refreshText(); });
        return p;
      }
      case "var": {
        const p = el("span", "pill varref");
        p.append(field(() => node.name, (s) => { node.name = s || "x"; }));
        return p;
      }
      case "unary": {
        const p = el("span", "pill op");
        p.append(el("span", "kw", node.op), renderExpr(node.operand));
        return p;
      }
      case "binary": {
        const p = el("span", "pill op");
        p.append(renderExpr(node.lhs), el("span", "kw", node.op), renderExpr(node.rhs));
        return p;
      }
      case "call": {
        const p = el("span", "pill call");
        p.append(el("span", "kw", node.callee + " ("));
        node.args.forEach((a, i) => { if (i) p.append(el("span", "kw", ",")); p.append(renderExpr(a)); });
        p.append(el("span", "kw", ")"));
        return p;
      }
    }
    return el("span", "pill lit", "?");
  }

  function renderStmtList(list) {
    const s = el("div", "stack");
    list.forEach((st) => s.append(renderStmt(st)));
    return s;
  }

  function renderStmt(node) {
    let blk;
    if (node.block === "let" || node.block === "assign") {
      blk = el("div", "block var");
      const row = el("div", "hdr");
      row.append(el("span", "label", node.block === "let" ? "设" : "赋"));
      row.append(field(() => node.name, (s) => { node.name = s || "x"; }));
      if (node.block === "let") row.append(el("span", "kw", ": " + node.type));
      row.append(el("span", "kw", "="), renderExpr(node.value));
      blk.append(row);
    } else if (node.block === "if") {
      blk = el("div", "block ctrl");
      const row = el("div", "hdr");
      row.append(el("span", "label", "如果"), renderExpr(node.cond), el("span", "kw", "那么"));
      blk.append(row);
      const m = el("div", "mouth"); m.append(renderStmtList(node.then)); blk.append(m);
      if (node.else) { blk.append(el("div", "hdr")); const m2 = el("div", "mouth"); m2.append(renderStmtList(node.else)); blk.append(m2); }
    } else if (node.block === "while") {
      blk = el("div", "block ctrl");
      const row = el("div", "hdr");
      row.append(el("span", "label", "重复直到非"), renderExpr(node.cond));
      blk.append(row);
      const m = el("div", "mouth"); m.append(renderStmtList(node.body)); blk.append(m);
    } else if (node.block === "return") {
      blk = el("div", "block ret");
      const row = el("div", "hdr");
      row.append(el("span", "label", "返回"));
      if (node.value) row.append(renderExpr(node.value));
      blk.append(row);
    } else if (node.block === "expr") {
      blk = el("div", "block ev");
      const row = el("div", "hdr"); row.append(renderExpr(node.expr)); blk.append(row);
    } else {
      blk = el("div", "block", JSON.stringify(node));
    }
    return blk;
  }

  function renderFn(fn) {
    const script = el("div", "script");
    script.style.left = fn._x + "px"; script.style.top = fn._y + "px";
    const blk = el("div", "block hat " + (fn.block === "extern_fn" ? "extern_fn" : "fn"));
    const hdr = el("div", "hdr");
    hdr.append(el("span", "label", (fn.block === "extern_fn" ? "外部" : "定义")));
    hdr.append(field(() => fn.name, (s) => { fn.name = s || "f"; }));
    fn.params.forEach((p) => hdr.append(el("span", "param", p.name + ": " + p.type)));
    if (fn.ret && fn.ret !== "void") hdr.append(el("span", "kw", "→ " + fn.ret));
    blk.append(hdr);
    if (fn.body) { const m = el("div", "mouth"); m.append(renderStmtList(fn.body)); blk.append(m); }
    script.append(blk);

    hdr.addEventListener("pointerdown", (e) => {
      if (e.target.classList.contains("field")) return;
      selected = fn; markSelected();
      e.stopPropagation();
      const start = { mx: e.clientX, my: e.clientY, ox: fn._x, oy: fn._y };
      const onMove = (ev) => {
        fn._x = start.ox + (ev.clientX - start.mx) / view.k;
        fn._y = start.oy + (ev.clientY - start.my) / view.k;
        script.style.left = fn._x + "px"; script.style.top = fn._y + "px";
      };
      const onUp = () => { window.removeEventListener("pointermove", onMove); window.removeEventListener("pointerup", onUp); };
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", onUp);
    });
    script._fn = fn;
    return script;
  }

  function markSelected() {
    [...canvas.children].forEach((s) =>
      s.firstChild && s.firstChild.style.setProperty("outline",
        s._fn === selected ? "3px solid #ffd21a" : "none"));
  }

  function render() {
    canvas.innerHTML = "";
    sprite().program.forEach((fn) => canvas.append(renderFn(fn)));
    markSelected();
    const tag = document.getElementById("page-tag");
    if (tag) tag.textContent = sprite().name + " 的积木";
    refreshText();
  }

  // ---------------- 精灵列表（多精灵 / 多页积木） ----------------
  function costumeThumb(sp, isCurrent) {
    let data = null;
    if (isCurrent && ce) return ce.toDataURL();
    if (sp.costumes && sp.costumes[0] && sp.costumes[0].data) data = sp.costumes[0].data;
    if (!data) return null;
    const off = document.createElement("canvas");
    off.width = data.width; off.height = data.height;
    off.getContext("2d").putImageData(data, 0, 0);
    return off.toDataURL();
  }

  function renderSpriteBar() {
    const list = document.getElementById("sprite-list");
    list.innerHTML = "";
    project.sprites.forEach((sp, i) => {
      const card = el("div", "sprite-card" + (i === project.cur ? " sel" : ""));
      const thumb = el("div", "thumb");
      const url = costumeThumb(sp, i === project.cur);
      if (url) thumb.style.backgroundImage = "url(" + url + ")";
      else thumb.textContent = sp.icon || "🎭";
      card.append(thumb);
      const nm = el("div", "nm", sp.name);
      card.append(nm);
      if (project.sprites.length > 1) {
        const del = el("button", "del", "×");
        del.addEventListener("click", (e) => { e.stopPropagation(); delSprite(i); });
        card.append(del);
      }
      card.addEventListener("click", () => selectSprite(i));
      list.append(card);
    });
  }

  function selectSprite(i) {
    if (i === project.cur) return;
    if (ce) ce.setStore(project.sprites[i].costumes); // 切换造型集（会先存回旧的）
    project.cur = i;
    selected = sprite().program[0] || null;
    renderSpriteBar();
    render();
  }

  function addSprite() {
    if (ce) ce.setStore([]); // 先把当前精灵造型存回，再给新精灵一组空造型
    const n = project.sprites.length + 1;
    const sp = { name: "精灵" + n, icon: "🎭",
      program: [{ block: "fn", name: "update", params: [], ret: "void",
        body: [{ block: "expr", expr: { block: "call", callee: "print",
          args: [{ block: "int", value: 0 }] } }] }],
      costumes: [] };
    placeFns(sp.program);
    project.sprites.push(sp);
    project.cur = project.sprites.length - 1;
    // setStore 上面用的是临时空数组；这里改指到新精灵的造型集
    if (ce) ce.setStore(sp.costumes);
    selected = sp.program[0];
    renderSpriteBar(); render();
  }

  function delSprite(i) {
    if (project.sprites.length <= 1) return;
    project.sprites.splice(i, 1);
    if (project.cur >= project.sprites.length) project.cur = project.sprites.length - 1;
    if (ce) ce.setStore(sprite().costumes);
    selected = sprite().program[0] || null;
    renderSpriteBar(); render();
  }

  // ---------------- 调色板 ----------------
  const NEW = {
    let: () => ({ block: "let", name: "x", type: "int", value: { block: "int", value: 0 } }),
    if: () => ({ block: "if", cond: { block: "bool", value: true }, then: [] }),
    while: () => ({ block: "while", cond: { block: "bool", value: true }, body: [] }),
    return: () => ({ block: "return", value: { block: "int", value: 0 } }),
    print: () => ({ block: "expr", expr: { block: "call", callee: "print", args: [{ block: "int", value: 0 }] } }),
  };
  function addStmt(kind) {
    if (!selected || !selected.body) return;
    selected.body.push(NEW[kind]()); render();
  }
  function addFn() {
    const fn = { block: "fn", name: "fn" + (sprite().program.length + 1), params: [], ret: "int",
      body: [{ block: "return", value: { block: "int", value: 0 } }],
      _x: 40 - view.x / view.k + 60, _y: 40 - view.y / view.k + 60 };
    sprite().program.push(fn); selected = fn; render();
  }

  function buildPalette() {
    const pal = document.getElementById("palette");
    pal.innerHTML = "";
    const item = (label, color, onClick, iconName) => {
      const b = el("button", "pal-block");
      b.style.background = color;
      if (iconName) { const ic = el("span", "ic"); ic.innerHTML = window.SinIcons.svg(iconName, 16); b.append(ic); }
      else { const dot = el("span", "pal-dot"); dot.style.background = "rgba(255,255,255,.55)"; b.append(dot); }
      b.append(el("span", null, label));
      b.addEventListener("click", onClick);
      pal.append(b);
    };
    pal.append(el("h2", null, "自定义"));
    item("新建函数", "#FF6680", addFn, "plus");
    pal.append(el("h2", null, "变量"));
    item("设 变量", "#FF8C1A", () => addStmt("let"));
    pal.append(el("h2", null, "控制"));
    item("如果 …", "#FFAB19", () => addStmt("if"));
    item("重复直到 …", "#FFAB19", () => addStmt("while"));
    pal.append(el("h2", null, "外观"));
    item("返回 …", "#9966FF", () => addStmt("return"));
    item("print( … )", "#4C97FF", () => addStmt("print"));
    pal.append(el("h2", null, "提示"));
    const tip = el("div", null, "下方切换精灵=切换积木页。点脚本选中再加积木；数字/变量可点改，文本实时更新。");
    tip.style.cssText = "font-size:12px;color:#9aa3b5;padding:2px 4px;line-height:1.6";
    pal.append(tip);
  }

  // ---------------- 视图切换 ----------------
  function setupTabs() {
    const tabs = document.querySelectorAll("header .tabs button");
    tabs.forEach((t) => t.addEventListener("click", () => {
      tabs.forEach((x) => x.classList.remove("active"));
      t.classList.add("active");
      document.querySelectorAll(".view").forEach((v) => v.classList.remove("active"));
      document.getElementById(t.dataset.view).classList.add("active");
    }));
  }

  // ---------------- 造型画板 ----------------
  function setupCostume() {
    ce = new window.CostumeEditor({
      canvas: document.getElementById("paint-canvas"),
      listEl: document.getElementById("costume-list"),
      swatchesEl: document.getElementById("swatches"),
      toolButtons: {
        pencil: document.getElementById("tool-pencil"),
        eraser: document.getElementById("tool-eraser"),
      },
      sizeInput: document.getElementById("brush-size"),
    });
    document.getElementById("tool-pencil").addEventListener("click", () => ce.setTool("pencil"));
    document.getElementById("tool-eraser").addEventListener("click", () => ce.setTool("eraser"));
    document.getElementById("brush-size").addEventListener("input", (e) => ce.setSize(+e.target.value));
    document.getElementById("btn-clear").addEventListener("click", () => ce.clear());
    document.getElementById("btn-export").addEventListener("click", () => {
      const a = document.createElement("a"); a.href = ce.toDataURL(); a.download = "costume.png"; a.click();
    });
    ce.setTool("pencil");
    ce.setStore(sprite().costumes); // 绑定到当前精灵的造型集
  }

  // ---------------- 启动 ----------------
  if (window.SinIcons) window.SinIcons.inject(document);
  buildPalette();
  setupTabs();
  setupCostume();
  document.getElementById("add-sprite").addEventListener("click", addSprite);
  applyView();
  renderSpriteBar();
  render();
  window._sin = { project, sprite, refreshText, render, selectSprite, addSprite }; // 测试探针
})();
