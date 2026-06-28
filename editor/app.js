// app.js — 积木 IDE：多精灵 / 多页积木 + 无限画布 + 可编辑积木 + 实时文本写回
//
// 模型（积木树）就是前端的 AST，是唯一真相源。每个精灵拥有自己的一页积木
// （program）与一组造型（costumes）。切换精灵 = 切换积木页 + 造型集。
//   编辑积木 → 改模型 → BlockModel.modelToSource → 文本视图
(function () {
  "use strict";

  // ---------------- 项目 / 精灵 ----------------
  // 积木构造小助手
  const I = (v) => ({ block: "int", value: v });
  const F = (v) => ({ block: "float", value: v });
  const S = (v) => ({ block: "string", value: v });
  const Vr = (n) => ({ block: "var", name: n });
  const C = (callee, ...args) => ({ block: "call", callee, args });
  const Bn = (op, lhs, rhs) => ({ block: "binary", op, lhs, rhs });
  const Ex = (expr) => ({ block: "expr", expr });

  function sampleMain() {
    return { block: "fn", name: "main", params: [], ret: "int",
      body: [{ block: "return", value: { block: "int", value: 0 } }] };
  }
  // 一个可在预览里玩的小程序：方向键移动精灵
  function sampleGame() {
    return { block: "fn", name: "main", params: [], ret: "int", body: [
      Ex(C("stage_init", I(480), I(360))),
      { block: "let", name: "hero", type: "int", len: 0, value: C("sprite_new", F(0), F(0), F(40)) },
      { block: "let", name: "x", type: "float", len: 0, value: F(0) },
      { block: "while", cond: C("stage_running"), body: [
        { block: "if", cond: C("key_down", C("key_left")), then: [
          { block: "assign", name: "x", value: Bn("-", Vr("x"), F(6)) } ] },
        { block: "if", cond: C("key_down", C("key_right")), then: [
          { block: "assign", name: "x", value: Bn("+", Vr("x"), F(6)) } ] },
        Ex(C("sprite_move_to", Vr("hero"), Vr("x"), F(0))),
        Ex(C("frame_begin")),
        Ex(C("sprite_draw", Vr("hero"))),
        Ex(C("draw_text", S("← → 移动我"), F(-200), F(150), I(20))),
        Ex(C("frame_end")),
      ] },
      Ex(C("stage_close")),
      { block: "return", value: I(0) },
    ] };
  }
  // 自动来回弹的精灵（无需输入），用于展示多精灵并行 + 造型纹理
  // 用 sprite_load 载入造型 PNG（coin.png）：预览画的就是这张造型，导出成品亦然。
  function sampleBounce() {
    return { block: "fn", name: "main", params: [], ret: "int", body: [
      Ex(C("stage_init", I(480), I(360))),
      { block: "let", name: "b", type: "int", len: 0, value: C("sprite_load", S("coin.png")) },
      { block: "let", name: "bx", type: "float", len: 0, value: F(150) },
      { block: "let", name: "vx", type: "float", len: 0, value: F(4) },
      { block: "while", cond: C("stage_running"), body: [
        { block: "assign", name: "bx", value: Bn("+", Vr("bx"), Vr("vx")) },
        { block: "if", cond: Bn(">", Vr("bx"), F(200)), then: [ { block: "assign", name: "vx", value: Bn("-", F(0), Vr("vx")) } ] },
        { block: "if", cond: Bn("<", Vr("bx"), F(-200)), then: [ { block: "assign", name: "vx", value: Bn("-", F(0), Vr("vx")) } ] },
        Ex(C("sprite_move_to", Vr("b"), Vr("bx"), F(-100))),
        Ex(C("frame_begin")),
        Ex(C("sprite_draw", Vr("b"))),
        Ex(C("frame_end")),
      ] },
      Ex(C("stage_close")),
      { block: "return", value: I(0) },
    ] };
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
      { name: "精灵2", icon: "🎮", program: [sampleGame()], costumes: [] },
      { name: "弹球", icon: "⚽", program: [sampleBounce()], costumes: [] },
    ];
    sprites.forEach((s) => placeFns(s.program));
    // structs / globals 为「项目级共享状态」：所有精灵共用同一组结构体/全局变量
    return { sprites, cur: 0, structs: [], globals: [] };
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
  const textHl = document.querySelector("#text-hl code");
  function fullModel() {
    // structs / globals 来自项目级共享状态，program 来自当前精灵
    return { structs: project.structs || [], globals: project.globals || [], program: sprite().program };
  }
  function setTextValue(v) { textOut.value = v; syncHighlight(); }
  function refreshText() {
    if (document.activeElement === textOut) return; // 用户正在编辑文本，别打断
    try { setTextValue(window.BlockModel.modelToSource(fullModel())); }
    catch (err) { setTextValue("// 序列化错误: " + err.message); }
    schedulePreview();
  }

  // ---------------- 语法高亮（透明 textarea 覆盖在高亮层上） ----------------
  const HL_KW = new Set(["let", "fn", "if", "else", "while", "for", "return", "extern", "struct", "true", "false", "in"]);
  const HL_TY = new Set(["int", "float", "bool", "string", "void"]);
  function hlEsc(t) { return t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;"); }
  function hlSpan(cls, t) { return '<span class="hl-' + cls + '">' + hlEsc(t) + "</span>"; }
  function highlight(src) {
    let out = "", i = 0; const n = src.length;
    const isW = (c) => /[A-Za-z0-9_]/.test(c);
    while (i < n) {
      const c = src[i];
      if (c === "/" && src[i + 1] === "/") { let j = i; while (j < n && src[j] !== "\n") j++; out += hlSpan("cm", src.slice(i, j)); i = j; }
      else if (c === '"') { let j = i + 1; while (j < n && src[j] !== '"') { if (src[j] === "\\") j++; j++; } j = Math.min(j + 1, n); out += hlSpan("st", src.slice(i, j)); i = j; }
      else if (/[0-9]/.test(c)) { let j = i; while (j < n && /[0-9.]/.test(src[j])) j++; out += hlSpan("nu", src.slice(i, j)); i = j; }
      else if (/[A-Za-z_]/.test(c)) { let j = i; while (j < n && isW(src[j])) j++; const w = src.slice(i, j); const cls = HL_KW.has(w) ? "kw" : HL_TY.has(w) ? "ty" : null; out += cls ? hlSpan(cls, w) : hlEsc(w); i = j; }
      else { out += hlEsc(c); i++; }
    }
    return out;
  }
  function syncHighlight() {
    if (textHl) textHl.innerHTML = highlight(textOut.value) + "\n";
    const wrap = document.getElementById("text-hl");
    if (wrap) { wrap.scrollTop = textOut.scrollTop; wrap.scrollLeft = textOut.scrollLeft; }
  }

  // ---------------- 实时预览（解释器跑积木） ----------------
  let preview = null, previewTimer = null;
  function setPvStatus(t, cls) {
    const e = document.getElementById("pv-status"); if (e) { e.textContent = t; e.className = cls || ""; }
  }
  // 造型资源：把所有精灵画板里的造型注册为「按名可取的真实图像」，
  // 这样预览里 sprite_load(name) 画的就是画板造型本身；同时设 assetBase="assets/"，
  // 让按文件名加载的造型 PNG（与导出成品同名）也能解析——预览所见即成品所见。
  function buildAssets() {
    const map = new Map();
    project.sprites.forEach((sp) => {
      (sp.costumes || []).forEach((c) => {
        if (!c || !c.data) return;
        const off = document.createElement("canvas");
        off.width = c.data.width; off.height = c.data.height;
        off.getContext("2d").putImageData(c.data, 0, 0);
        map.set(c.name, off);
        if (!/\.png$/i.test(c.name)) map.set(c.name + ".png", off);
      });
    });
    return map;
  }
  function runPreview() {
    if (!preview) return;
    preview.setAssets("assets/", buildAssets()); // 造型同源：画板造型 + 文件造型
    // 多精灵并行 + 项目级共享状态（结构体/全局变量/数组）
    try { preview.runProject(project.sprites.map((s) => s.program), setPvStatus,
      { globals: project.globals || [], structs: project.structs || [] }); }
    catch (e) { setPvStatus("预览错误", "warn"); }
  }
  function schedulePreview() { clearTimeout(previewTimer); previewTimer = setTimeout(runPreview, 450); }
  function setupPreview() {
    const c = document.getElementById("preview-canvas");
    if (!c || !window.SinPreview) return;
    preview = new window.SinPreview(c);
    const run = document.getElementById("pv-run"), stop = document.getElementById("pv-stop");
    if (run) run.addEventListener("click", runPreview);
    if (stop) stop.addEventListener("click", () => { preview.stop(); setPvStatus("已停止", ""); });
    c.addEventListener("pointerdown", () => c.focus());
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
      case "string": {
        const p = el("span", "pill lit");
        p.append(el("span", "kw", '"'));
        p.append(field(() => node.value, (s) => { node.value = s; }));
        p.append(el("span", "kw", '"'));
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
      case "index": {
        const p = el("span", "pill varref");
        p.append(renderExpr(node.arr), el("span", "kw", "["), renderExpr(node.idx), el("span", "kw", "]"));
        return p;
      }
      case "array": {
        const p = el("span", "pill lit");
        p.append(el("span", "kw", "["));
        node.elems.forEach((a, i) => { if (i) p.append(el("span", "kw", ",")); p.append(renderExpr(a)); });
        p.append(el("span", "kw", "]"));
        return p;
      }
      case "field": {
        const p = el("span", "pill varref");
        p.append(renderExpr(node.obj), el("span", "kw", "."), el("span", null, node.name));
        return p;
      }
      case "structlit": {
        const p = el("span", "pill call");
        p.append(el("span", "kw", node.typeName + " {"));
        node.fields.forEach((f, i) => {
          if (i) p.append(el("span", "kw", ","));
          p.append(el("span", "kw", f.name + ":"), renderExpr(f.value));
        });
        p.append(el("span", "kw", "}"));
        return p;
      }
    }
    return el("span", "pill lit", "?");
  }

  function renderStmtList(list) {
    const s = el("div", "stack");
    s._list = list;                 // 作为拖拽重排的落点（drop zone）
    list.forEach((st) => s.append(renderStmt(st, list)));
    return s;
  }

  function renderStmt(node, list) {
    let blk;
    if (node.block === "let" || node.block === "assign") {
      blk = el("div", "block var");
      const row = el("div", "hdr");
      row.append(el("span", "label", node.block === "let" ? "设" : "赋"));
      row.append(field(() => node.name, (s) => { node.name = s || "x"; }));
      if (node.block === "let") {
        const t = node.type + (node.len > 0 ? "[" + node.len + "]" : "");
        row.append(el("span", "kw", ": " + t));
      } else if (node.index) {
        row.append(el("span", "kw", "["), renderExpr(node.index), el("span", "kw", "]"));
      }
      if (node.value !== undefined) row.append(el("span", "kw", "="), renderExpr(node.value));
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
    } else if (node.block === "for") {
      blk = el("div", "block ctrl");
      const row = el("div", "hdr");
      row.append(el("span", "label", "对"), field(() => node.var, (s) => { node.var = s || "i"; }),
        el("span", "kw", "从"), renderExpr(node.start), el("span", "kw", ".."), renderExpr(node.end));
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
    // 拖拽重排：按住语句积木拖动，可在各 stack（函数体 / 控制块嘴巴）间移动
    blk.addEventListener("pointerdown", (e) => {
      if (e.target.classList.contains("field")) return;
      if (list) startStmtDrag(node, list, blk, e);
    });
    return blk;
  }

  // ---------------- 语句积木拖拽重排 ----------------
  let drag = null;
  function startStmtDrag(node, fromList, blockEl, e) {
    e.stopPropagation();
    const r = blockEl.getBoundingClientRect();
    const ghost = blockEl.cloneNode(true);
    Object.assign(ghost.style, {
      position: "fixed", left: r.left + "px", top: r.top + "px", width: r.width + "px",
      pointerEvents: "none", opacity: ".9", zIndex: 9999, transform: "rotate(2deg)",
      boxShadow: "0 8px 20px rgba(0,0,0,.3)",
    });
    document.body.appendChild(ghost);
    blockEl.style.opacity = ".25";
    const indicator = el("div", "drop-indicator");
    drag = { node, fromList, blockEl, ghost, indicator, target: null,
      offx: e.clientX - r.left, offy: e.clientY - r.top };

    const move = (ev) => {
      ghost.style.left = (ev.clientX - drag.offx) + "px";
      ghost.style.top = (ev.clientY - drag.offy) + "px";
      updateDropTarget(ev);
    };
    const up = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
      ghost.remove();
      if (indicator.parentNode) indicator.remove();
      finishDrop();
      drag = null;
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
  }

  function updateDropTarget(ev) {
    const stacks = [...canvas.querySelectorAll(".stack")];
    let best = null;
    for (const st of stacks) {
      if (drag.blockEl.contains(st)) continue;       // 不能放进自身子树
      const r = st.getBoundingClientRect();
      if (ev.clientX >= r.left - 12 && ev.clientX <= r.right + 12 &&
          ev.clientY >= r.top - 24 && ev.clientY <= r.bottom + 24) { best = st; break; }
    }
    if (!best) { drag.target = null; if (drag.indicator.parentNode) drag.indicator.remove(); return; }
    const kids = [...best.children].filter((c) => c.classList.contains("block"));
    let idx = kids.length;
    for (let i = 0; i < kids.length; i++) {
      const r = kids[i].getBoundingClientRect();
      if (ev.clientY < r.top + r.height / 2) { idx = i; break; }
    }
    best.insertBefore(drag.indicator, kids[idx] || null);
    drag.target = { list: best._list, index: idx };
  }

  function finishDrop() {
    if (!drag.target) { render(); return; }
    const fromIdx = drag.fromList.indexOf(drag.node);
    if (fromIdx < 0) { render(); return; }
    drag.fromList.splice(fromIdx, 1);
    let idx = drag.target.index;
    if (drag.target.list === drag.fromList && fromIdx < idx) idx--;
    drag.target.list.splice(idx, 0, drag.node);
    render();
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

  function renderCanvas() {
    canvas.innerHTML = "";
    sprite().program.forEach((fn) => canvas.append(renderFn(fn)));
    markSelected();
    const tag = document.getElementById("page-tag");
    if (tag) tag.textContent = sprite().name + " 的积木";
  }
  function render() { renderCanvas(); refreshText(); }

  // ---------------- 反向同步：文本 → 积木（wasm 编译器） ----------------
  let sincMod = null;
  if (window.SincModule) {
    window.SincModule().then((m) => { sincMod = m; window.__sincReady = true; })
      .catch(() => setTextStatus("反向解析不可用（请用 HTTP 打开）", "warn"));
  }

  function setTextStatus(text, cls) {
    const el2 = document.getElementById("text-status");
    if (el2) { el2.textContent = text; el2.className = cls || ""; }
  }
  let textTimer = null;
  function onTextEdited() {
    if (!sincMod) { setTextStatus("编译器加载中…", "warn"); return; }
    let res;
    try {
      const out = sincMod.ccall("sin_to_blocks", "string", ["string"], [textOut.value]);
      res = JSON.parse(out);
    } catch (e) { setTextStatus("解析失败", "warn"); return; }
    const blk = res.blocks || {};
    const prog = blk.program || [];
    // 尽量保留同名函数的画布位置
    const oldPos = {};
    sprite().program.forEach((f) => { oldPos[f.name] = { x: f._x, y: f._y }; });
    prog.forEach((f) => { if (oldPos[f.name]) { f._x = oldPos[f.name].x; f._y = oldPos[f.name].y; } });
    placeFns(prog);
    // 结构体/全局写回到项目级共享状态（编辑任一精灵的文本都更新共享状态）
    project.structs = blk.structs || [];
    project.globals = blk.globals || [];
    sprite().program = prog;
    selected = prog[0] || null;
    renderCanvas(); // 不回写文本，避免打断输入
    const n = (res.diags || []).length;
    setTextStatus(n ? (n + " 个问题（部分有效）") : "已同步 ✓", n ? "warn" : "ok");
    schedulePreview(); // 文本编辑也实时刷新预览
  }
  textOut.addEventListener("input", () => {
    syncHighlight();                 // 即时高亮
    clearTimeout(textTimer);
    textTimer = setTimeout(onTextEdited, 250);
  });
  textOut.addEventListener("scroll", syncHighlight);

  // ---------------- 一键导出 .sin（自动补运行时声明，使其可独立编译） ----------------
  const RUNTIME_EXTERN_DECLS = [
    ["stage_init", "extern fn stage_init(w: int, h: int)"],
    ["stage_running", "extern fn stage_running() -> bool"],
    ["frame_begin", "extern fn frame_begin()"],
    ["frame_end", "extern fn frame_end()"],
    ["stage_close", "extern fn stage_close()"],
    ["sprite_new", "extern fn sprite_new(x: float, y: float, size: float) -> int"],
    ["sprite_load", "extern fn sprite_load(path: string) -> int"],
    ["sprite_move_to", "extern fn sprite_move_to(s: int, x: float, y: float)"],
    ["sprite_x", "extern fn sprite_x(s: int) -> float"],
    ["sprite_y", "extern fn sprite_y(s: int) -> float"],
    ["sprite_draw", "extern fn sprite_draw(s: int)"],
    ["key_down", "extern fn key_down(key: int) -> bool"],
    ["key_left", "extern fn key_left() -> int"],
    ["key_right", "extern fn key_right() -> int"],
    ["key_up", "extern fn key_up() -> int"],
    ["key_down_arrow", "extern fn key_down_arrow() -> int"],
    ["say", "extern fn say(s: int, text: string)"],
    ["draw_text", "extern fn draw_text(text: string, x: float, y: float, size: int)"],
    ["draw_number", "extern fn draw_number(n: int, x: float, y: float, size: int)"],
    ["sound_load", "extern fn sound_load(path: string) -> int"],
    ["play_sound", "extern fn play_sound(snd: int)"],
    ["play_tone", "extern fn play_tone(freq: int, ms: int)"],
    ["broadcast", "extern fn broadcast(message: string)"],
    ["received", "extern fn received(message: string) -> bool"],
    ["to_float", "extern fn to_float(n: int) -> float"],
    ["to_int", "extern fn to_int(f: float) -> int"],
  ];
  function exportSource() {
    const model = fullModel();
    const have = new Set((model.program || []).map((f) => f.name));
    const externs = RUNTIME_EXTERN_DECLS.filter(([name]) => !have.has(name)).map(([, t]) => t);
    const head = externs.length ? "// 运行时声明（导出自动补全，使程序可独立编译）\n" + externs.join("\n") + "\n\n" : "";
    return head + window.BlockModel.modelToSource(model);
  }
  function exportSin() {
    const src = exportSource();
    const blob = new Blob([src], { type: "text/plain;charset=utf-8" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = (sprite().name || "program") + ".sin";
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }
  window._sinExport = exportSource; // 供测试读取

  // ---------------- 保存 / 打开项目（.sinproj） ----------------
  // 项目文件 = 全部精灵（积木页 + 造型）+ 项目级共享状态 + 上次发布配置。
  // 造型 ImageData 以 PNG dataURL 存储，重新打开即可继续编辑。
  function imageDataToPNG(d) {
    const off = document.createElement("canvas");
    off.width = d.width; off.height = d.height;
    off.getContext("2d").putImageData(d, 0, 0);
    return off.toDataURL("image/png");
  }
  function pngToImageData(url) {
    return new Promise((resolve) => {
      const img = new Image();
      img.onload = () => {
        const off = document.createElement("canvas");
        off.width = img.naturalWidth || 64; off.height = img.naturalHeight || 64;
        const cx = off.getContext("2d"); cx.drawImage(img, 0, 0);
        resolve(cx.getImageData(0, 0, off.width, off.height));
      };
      img.onerror = () => resolve(null);
      img.src = url;
    });
  }
  function serializeProject() {
    if (ce) ce.flush(); // 把当前画布未提交的笔画写回造型
    return {
      format: "sincoding-project", version: 1,
      cur: project.cur,
      structs: project.structs || [], globals: project.globals || [],
      publish: project.publish || null,
      sprites: project.sprites.map((sp) => ({
        name: sp.name, icon: sp.icon || "🎭",
        program: (sp.program || []).map((fn) => stripPos(fn)),
        costumes: (sp.costumes || []).map((c) => ({
          name: c.name, png: c.data ? imageDataToPNG(c.data) : null,
        })),
      })),
    };
  }
  // 去掉画布坐标等运行期字段，保留纯 AST（位置在加载时重排）
  function stripPos(fn) { const { _x, _y, ...rest } = fn; return rest; }

  function saveProject() {
    const data = JSON.stringify(serializeProject(), null, 2);
    const blob = new Blob([data], { type: "application/json" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = (project.sprites[0] && project.sprites[0].name ? "project" : "project") + ".sinproj";
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }

  async function loadProject(obj) {
    if (!obj || obj.format !== "sincoding-project" || !Array.isArray(obj.sprites)) {
      alert("不是有效的 .sinproj 项目文件"); return false;
    }
    const sprites = [];
    for (const sp of obj.sprites) {
      const costumes = [];
      for (const c of (sp.costumes || [])) {
        costumes.push({ name: c.name || "造型1", data: c.png ? await pngToImageData(c.png) : null });
      }
      const program = (sp.program || []).map((f) => ({ ...f }));
      placeFns(program);
      sprites.push({ name: sp.name || "精灵", icon: sp.icon || "🎭", program, costumes });
    }
    if (sprites.length === 0) { alert("项目里没有精灵"); return false; }
    // 原地替换 project（保持闭包引用）
    project.sprites.length = 0; project.sprites.push(...sprites);
    project.cur = Math.min(obj.cur || 0, sprites.length - 1);
    project.structs = obj.structs || []; project.globals = obj.globals || [];
    project.publish = obj.publish || null;
    selected = sprite().program[0] || null;
    if (ce) ce.setStore(sprite().costumes);
    renderSpriteBar(); render(); schedulePreview();
    return true;
  }
  window._sinLoadProject = loadProject;   // 供测试调用
  window._sinSerializeProject = serializeProject;

  function openProjectFile(file) {
    const r = new FileReader();
    r.onload = () => { try { loadProject(JSON.parse(r.result)); } catch (e) { alert("解析项目失败: " + e.message); } };
    r.readAsText(file);
  }

  // ---------------- 发布（多平台一键编译） ----------------
  // 浏览器不能交叉编译，发布经本地构建服务（tools/ide_server.py 的 /api/publish）
  // 调用既有 build_*.sh。无服务（file:// 或纯静态托管）时，降级为下载 .sin 源码 + 提示。
  function setupPublish() {
    const modal = document.getElementById("publish-modal");
    if (!modal) return;
    const openBtn = document.getElementById("btn-publish");
    const closeBtns = [document.getElementById("publish-close"), document.getElementById("publish-cancel")];
    const goBtn = document.getElementById("publish-go");
    const statusEl = document.getElementById("pub-status");
    const resultsEl = document.getElementById("pub-results");
    const logoInput = document.getElementById("pub-logo-input");
    const logoBtn = document.getElementById("pub-logo-btn");
    const logoPrev = document.getElementById("pub-logo-preview");
    const logoName = document.getElementById("pub-logo-name");
    let logoDataURL = null;

    const show = (v) => { modal.hidden = !v; };
    if (openBtn) openBtn.addEventListener("click", () => {
      const p = project.publish || {};
      if (p.name) document.getElementById("pub-name").value = p.name;
      if (p.pkg) document.getElementById("pub-pkg").value = p.pkg;
      statusEl.textContent = ""; resultsEl.hidden = true; resultsEl.innerHTML = "";
      show(true);
    });
    closeBtns.forEach((b) => b && b.addEventListener("click", () => show(false)));
    modal.addEventListener("click", (e) => { if (e.target === modal) show(false); });
    if (logoBtn) logoBtn.addEventListener("click", () => logoInput.click());
    if (logoInput) logoInput.addEventListener("change", () => {
      const f = logoInput.files[0]; if (!f) return;
      const r = new FileReader();
      r.onload = () => { logoDataURL = r.result; logoPrev.src = logoDataURL; logoPrev.hidden = false; logoName.textContent = f.name; };
      r.readAsDataURL(f);
    });

    function setRows(rows) {
      resultsEl.hidden = false;
      resultsEl.innerHTML = "";
      rows.forEach((r) => {
        const div = el("div", "row " + (r.ok ? "ok" : (r.pending ? "" : "err")));
        const badge = el("span", "badge", r.pending ? "…" : (r.ok ? "✓" : "✗"));
        div.append(badge, el("span", "lbl", r.label));
        if (r.href) { const a = document.createElement("a"); a.href = r.href; a.textContent = r.linkText || "下载"; a.target = "_blank"; if (r.download) a.download = r.download; div.append(a); }
        resultsEl.append(div);
      });
    }

    if (goBtn) goBtn.addEventListener("click", async () => {
      const name = document.getElementById("pub-name").value.trim() || "Game";
      const pkg = document.getElementById("pub-pkg").value.trim() || "org.sincoding.game";
      const platforms = [...document.querySelectorAll(".pub-plat:checked")].map((c) => c.value);
      if (platforms.length === 0) { statusEl.textContent = "请至少选择一个平台"; return; }
      project.publish = { name, pkg, platforms }; // 记住配置，随项目保存
      const source = exportSource();
      goBtn.disabled = true; statusEl.textContent = "编译中…";
      setRows(platforms.map((p) => ({ label: platLabel(p), pending: true })));
      try {
        const resp = await fetch("api/publish", {
          method: "POST", headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ name, pkg, platforms, source, logo: logoDataURL,
            assets: collectAssets() }),
        });
        if (!resp.ok) throw new Error("HTTP " + resp.status);
        const out = await resp.json();
        const rows = (out.results || []).map((r) => ({
          label: platLabel(r.platform) + (r.ok ? "" : "：" + (r.error || "失败")),
          ok: r.ok, href: r.artifact || null,   // 服务端已返回相对 editor 的路径
          linkText: r.platform === "web" ? "打开/下载" : "下载", download: r.platform === "web" ? null : (r.artifact ? r.artifact.split("/").pop() : null),
        }));
        setRows(rows);
        statusEl.textContent = out.results && out.results.every((r) => r.ok) ? "发布完成 ✓" : "部分平台失败";
      } catch (e) {
        // 降级：没有本地构建服务——下载 .sin 源码并提示用构建脚本
        const blob = new Blob([source], { type: "text/plain" });
        const url = URL.createObjectURL(blob);
        setRows([{ label: "未连接本地构建服务，已改为下载源码", ok: false,
          href: url, linkText: name + ".sin", download: name + ".sin" }]);
        statusEl.textContent = "提示：用 tools/ide_server.py 启动可一键编译；或用 tools/build_*.sh 手动编译";
      } finally { goBtn.disabled = false; }
    });
  }
  function platLabel(p) { return ({ web: "Web (wasm)", linux: "Linux", windows: "Windows (.exe)", android: "Android (.apk)" })[p] || p; }
  // 收集预览/成品需要的造型资源（文件名 → PNG dataURL），随发布请求一起送给构建服务
  function collectAssets() {
    const out = {};
    project.sprites.forEach((sp) => (sp.costumes || []).forEach((c) => {
      if (c && c.data) { const nm = /\.png$/i.test(c.name) ? c.name : c.name + ".png"; out[nm] = imageDataToPNG(c.data); }
    }));
    return out;
  }

  // ---------------- 精灵列表（多精灵 / 多页积木） ----------------
  // 造型是否有绘制内容（非全透明），用于判断是否以造型为纹理
  function spriteHasArt(sp, isCurrent) {
    let d = null;
    if (isCurrent && ce) {
      const c = ce.canvas; d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data;
    } else if (sp.costumes && sp.costumes[0] && sp.costumes[0].data) {
      d = sp.costumes[0].data.data;
    }
    if (!d) return false;
    for (let i = 3; i < d.length; i += 4) if (d[i] > 8) return true;
    return false;
  }

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
    let: () => ({ block: "let", name: "x", type: "int", len: 0, value: { block: "int", value: 0 } }),
    let_str: () => ({ block: "let", name: "s", type: "string", len: 0, value: { block: "string", value: "你好" } }),
    let_arr: () => ({ block: "let", name: "a", type: "int", len: 3,
      value: { block: "array", elems: [{ block: "int", value: 0 }, { block: "int", value: 0 }, { block: "int", value: 0 }] } }),
    set_idx: () => ({ block: "assign", name: "a", index: { block: "int", value: 0 }, value: { block: "int", value: 0 } }),
    if: () => ({ block: "if", cond: { block: "bool", value: true }, then: [] }),
    while: () => ({ block: "while", cond: { block: "bool", value: true }, body: [] }),
    for: () => ({ block: "for", var: "i", start: { block: "int", value: 0 }, end: { block: "int", value: 10 }, body: [] }),
    if_else: () => ({ block: "if", cond: { block: "bool", value: true }, then: [], else: [] }),
    return: () => ({ block: "return", value: { block: "int", value: 0 } }),
    print: () => ({ block: "expr", expr: { block: "call", callee: "print", args: [{ block: "int", value: 0 }] } }),
    // 运算 / 数据
    incr: () => ({ block: "assign", name: "x", value: Bn("+", Vr("x"), I(1)) }),
    decr: () => ({ block: "assign", name: "x", value: Bn("-", Vr("x"), I(1)) }),
    set_op: () => ({ block: "assign", name: "x", value: Bn("+", Vr("x"), I(1)) }),
    to_int: () => ({ block: "let", name: "n", type: "int", len: 0, value: C("to_int", F(0)) }),
    to_float: () => ({ block: "let", name: "f", type: "float", len: 0, value: C("to_float", I(0)) }),
    // 运动（精灵）
    sprite_new: () => ({ block: "let", name: "s", type: "int", len: 0, value: C("sprite_new", F(0), F(0), F(40)) }),
    sprite_move: () => Ex(C("sprite_move_to", Vr("s"), F(0), F(0))),
    sprite_x: () => ({ block: "let", name: "px", type: "float", len: 0, value: C("sprite_x", Vr("s")) }),
    sprite_y: () => ({ block: "let", name: "py", type: "float", len: 0, value: C("sprite_y", Vr("s")) }),
    // 外观
    sprite_load: () => ({ block: "let", name: "s", type: "int", len: 0, value: C("sprite_load", S("ball.png")) }),
    sprite_draw: () => Ex(C("sprite_draw", Vr("s"))),
    say: () => Ex(C("say", Vr("s"), S("你好"))),
    draw_text: () => Ex(C("draw_text", S("文字"), F(0), F(0), I(24))),
    draw_number: () => Ex(C("draw_number", Vr("x"), F(0), F(0), I(24))),
    // 舞台
    stage_init: () => Ex(C("stage_init", I(800), I(600))),
    game_loop: () => ({ block: "while", cond: C("stage_running"),
      body: [Ex(C("frame_begin")), Ex(C("frame_end"))] }),
    frame_begin: () => Ex(C("frame_begin")),
    frame_end: () => Ex(C("frame_end")),
    stage_close: () => Ex(C("stage_close")),
    // 事件 / 输入 / 声音
    if_key: () => ({ block: "if", cond: C("key_down", C("key_left")), then: [] }),
    if_key_right: () => ({ block: "if", cond: C("key_down", C("key_right")), then: [] }),
    if_key_up: () => ({ block: "if", cond: C("key_down", C("key_up")), then: [] }),
    if_key_down: () => ({ block: "if", cond: C("key_down", C("key_down_arrow")), then: [] }),
    broadcast: () => Ex(C("broadcast", S("go"))),
    if_received: () => ({ block: "if", cond: C("received", S("go")), then: [] }),
    play_tone: () => Ex(C("play_tone", I(440), I(200))),
    play_sound: () => Ex(C("play_sound", I(0))),
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
    pal.append(el("h2", null, "变量 / 数据"));
    item("设 变量", "#FF8C1A", () => addStmt("let"));
    item("设 字符串", "#FF8C1A", () => addStmt("let_str"));
    item("设 数组", "#FF8C1A", () => addStmt("let_arr"));
    item("数组赋值 a[i]=v", "#FF8C1A", () => addStmt("set_idx"));
    pal.append(el("h2", null, "运算"));
    item("变量 +1", "#59C059", () => addStmt("incr"));
    item("变量 −1", "#59C059", () => addStmt("decr"));
    item("变量 ← 表达式", "#59C059", () => addStmt("set_op"));
    item("取整 to_int", "#59C059", () => addStmt("to_int"));
    item("转浮点 to_float", "#59C059", () => addStmt("to_float"));
    pal.append(el("h2", null, "控制"));
    item("如果 …", "#FFAB19", () => addStmt("if"));
    item("如果 … 否则 …", "#FFAB19", () => addStmt("if_else"));
    item("重复直到 …", "#FFAB19", () => addStmt("while"));
    item("for i in a..b", "#FFAB19", () => addStmt("for"));
    item("返回 …", "#9966FF", () => addStmt("return"));
    item("print( … )", "#4C97FF", () => addStmt("print"));
    pal.append(el("h2", null, "舞台"));
    item("初始化舞台", "#FFAB19", () => addStmt("stage_init"), "play");
    item("游戏主循环", "#FFAB19", () => addStmt("game_loop"), "play");
    item("开始绘制 frame", "#FFAB19", () => addStmt("frame_begin"));
    item("结束绘制 frame", "#FFAB19", () => addStmt("frame_end"));
    item("关闭舞台", "#FFAB19", () => addStmt("stage_close"), "stop");
    pal.append(el("h2", null, "运动（精灵）"));
    item("新建精灵", "#4C97FF", () => addStmt("sprite_new"));
    item("移动精灵到 x y", "#4C97FF", () => addStmt("sprite_move"), "move");
    item("取精灵 x", "#4C97FF", () => addStmt("sprite_x"));
    item("取精灵 y", "#4C97FF", () => addStmt("sprite_y"));
    pal.append(el("h2", null, "外观"));
    item("载入造型", "#9966FF", () => addStmt("sprite_load"), "palette");
    item("画出精灵", "#9966FF", () => addStmt("sprite_draw"));
    item("说 …", "#9966FF", () => addStmt("say"));
    item("画文字", "#9966FF", () => addStmt("draw_text"));
    item("画数字", "#9966FF", () => addStmt("draw_number"));
    pal.append(el("h2", null, "事件 / 声音"));
    item("当按← 如果", "#FFBF00", () => addStmt("if_key"));
    item("当按→ 如果", "#FFBF00", () => addStmt("if_key_right"));
    item("当按↑ 如果", "#FFBF00", () => addStmt("if_key_up"));
    item("当按↓ 如果", "#FFBF00", () => addStmt("if_key_down"));
    item("广播 “go”", "#FFBF00", () => addStmt("broadcast"));
    item("如果收到 “go”", "#FFBF00", () => addStmt("if_received"));
    item("播放音调", "#CF63CF", () => addStmt("play_tone"), "bell");
    item("播放声音", "#CF63CF", () => addStmt("play_sound"), "bell");
    pal.append(el("h2", null, "提示"));
    const tip = el("div", null, "下方切换精灵=切换积木页。点脚本选中再加积木；数字/变量可点改，文本实时更新。");
    tip.style.cssText = "font-size:12px;color:#9aa3b5;padding:2px 4px;line-height:1.6";
    pal.append(tip);
  }

  // ---------------- 舞台：精灵以造型为纹理 ----------------
  function renderStage() {
    const stage = document.getElementById("stage");
    if (!stage) return;
    stage.innerHTML = "";
    project.sprites.forEach((sp, i) => {
      if (sp._stageX === undefined) { sp._stageX = 200 + i * 150; sp._stageY = 240; }
      const d = el("div", "stage-sprite");
      const art = spriteHasArt(sp, i === project.cur);
      d.textContent = art ? "" : (sp.icon || "🎭");    // 有造型则以造型为纹理，否则占位
      if (art) {
        const url = costumeThumb(sp, i === project.cur);
        if (url) d.style.backgroundImage = "url(" + url + ")";
      }
      d.style.left = sp._stageX + "px"; d.style.top = sp._stageY + "px";
      d.title = sp.name;
      d.addEventListener("pointerdown", (e) => {
        e.preventDefault();
        const start = { mx: e.clientX, my: e.clientY, ox: sp._stageX, oy: sp._stageY };
        const mv = (ev) => {
          sp._stageX = start.ox + (ev.clientX - start.mx);
          sp._stageY = start.oy + (ev.clientY - start.my);
          d.style.left = sp._stageX + "px"; d.style.top = sp._stageY + "px";
        };
        const up = () => { window.removeEventListener("pointermove", mv); window.removeEventListener("pointerup", up); };
        window.addEventListener("pointermove", mv); window.addEventListener("pointerup", up);
      });
      stage.append(d);
    });
  }

  // ---------------- 视图切换 ----------------
  function setupTabs() {
    const tabs = document.querySelectorAll("header .tabs button");
    tabs.forEach((t) => t.addEventListener("click", () => {
      tabs.forEach((x) => x.classList.remove("active"));
      t.classList.add("active");
      document.querySelectorAll(".view").forEach((v) => v.classList.remove("active"));
      document.getElementById(t.dataset.view).classList.add("active");
      if (t.dataset.view === "stage-view") renderStage(); // 进入舞台时按最新造型渲染
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
  setupPreview();
  setupPublish();
  const expBtn = document.getElementById("btn-export-sin");
  if (expBtn) expBtn.addEventListener("click", exportSin);
  const saveBtn = document.getElementById("btn-save-proj");
  if (saveBtn) saveBtn.addEventListener("click", saveProject);
  const openBtn = document.getElementById("btn-open-proj");
  const openInput = document.getElementById("open-proj-input");
  if (openBtn && openInput) {
    openBtn.addEventListener("click", () => openInput.click());
    openInput.addEventListener("change", () => { if (openInput.files[0]) openProjectFile(openInput.files[0]); openInput.value = ""; });
  }
  document.getElementById("add-sprite").addEventListener("click", addSprite);
  applyView();
  renderSpriteBar();
  render();
  window._sin = { project, sprite, refreshText, render, selectSprite, addSprite }; // 测试探针
})();
