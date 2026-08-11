// app.js — 积木 IDE：多精灵 / 多页积木 + 无限画布 + 可编辑积木 + 实时文本写回
//
// 模型（积木树）就是前端的 AST，是唯一真相源。每个精灵拥有自己的一页积木
// （program）与一组造型（costumes）。切换精灵 = 切换积木页 + 造型集。
//   编辑积木 → 改模型 → 引擎 sin_blocks_to_src → 文本视图（序列化只有 C++ 一份实现）
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
    // imports / structs / globals 都是项目级共享状态，program 来自当前精灵。
    // imports 必须带上——漏掉会让积木写回时把用户的 import 行整行删掉。
    return { imports: project.imports || [], structs: project.structs || [],
             globals: project.globals || [], program: sprite().program };
  }
  function setTextValue(v) { textOut.value = v; syncHighlight(); }
  // 积木 → 文本：交给 wasm 引擎（唯一序列化器）。
  // 此前由 blockmodel.js 镜像 C++ 实现，双份实现已漂移出真实 bug（import/泛型/数组长度丢失），
  // 现已退役——宁可提示"引擎未就绪"，也不产出错误的文本。
  function modelToSource(model) {
    if (!sincMod) return null;
    const r = JSON.parse(sincMod.ccall("sin_blocks_to_src", "string", ["string"], [JSON.stringify(model)]));
    if (!r.ok) throw new Error(r.err || "序列化失败");
    return r.source;
  }
  function refreshText() {
    if (document.activeElement === textOut) return; // 用户正在编辑文本，别打断
    try {
      const src = modelToSource(fullModel());
      if (src === null) { setTextStatus("编译器加载中…", "warn"); return; }
      setTextValue(src);
    }
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
      { globals: project.globals || [], structs: project.structs || [],
        libImpl: project.libImpl || [] }); }
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
    // 执行高亮：解释器每执行一个积木就回调；按节点高亮其 DOM（节流，避免高频闪烁）
    preview.onStep = (node) => glowNode(node);
    // 控制台：print 输出实时打到舞台控制台
    preview.onPrint = (text) => conLog(text);
    // 调试：断点集合共用；暂停时弹出现场面板
    preview.breakpoints = breakpoints;
    preview.onPause = (node, env, stack) => showPausePanel(node, env, stack);
    const bp = document.getElementById("dbg-pause"), bs = document.getElementById("dbg-step"),
          br = document.getElementById("dbg-resume");
    if (bp) bp.addEventListener("click", () => { if (preview) preview.dbgPause(); });
    if (bs) bs.addEventListener("click", () => { hidePausePanel(); if (preview) preview.dbgStep(); });
    if (br) br.addEventListener("click", () => { hidePausePanel(); setPvStatus("运行中 ▶", "ok"); if (preview) preview.dbgResume(); });
  }

  // ---------------- 调试器（M5）：断点 / 单步 / 变量面板 / 调用栈 ----------------
  const breakpoints = new Set();     // 跨重绘保留（存 AST 节点引用）
  function toggleBreakpoint(node, blk) {
    if (breakpoints.has(node)) { breakpoints.delete(node); blk.classList.remove("bp"); }
    else { breakpoints.add(node); blk.classList.add("bp"); }
    if (preview) preview.breakpoints = breakpoints;   // 解释器与 UI 共用同一个集合
  }
  function dbgBtns(paused) {
    const st = document.getElementById("dbg-step"), rs = document.getElementById("dbg-resume");
    if (st) st.disabled = !paused;
    if (rs) rs.disabled = !paused;
  }
  function showPausePanel(node, env, stack) {
    const panel = document.getElementById("dbg-panel");
    const canvas = document.getElementById("preview-canvas");
    if (!panel) return;
    panel.hidden = false;
    if (canvas) canvas.hidden = true;                 // 暂停时用面板占位，便于查看现场
    const where = document.getElementById("dbg-where");
    if (where) where.textContent = (stack.length ? stack[stack.length - 1] + "()" : "main()") +
                                   " · " + (node.block || "?");
    // 变量
    const vars = document.getElementById("dbg-vars");
    if (vars) {
      vars.innerHTML = "";
      preview.dbgVars(env).forEach((v) => {
        const row = el("div", "dbg-row");
        row.append(el("span", "n", v.name), el("span", "v", fmtVal(v.value)), el("span", "sc", v.scope));
        vars.append(row);
      });
      if (!vars.children.length) vars.append(el("div", "dbg-frame", "（无）"));
    }
    // 调用栈（内层在上）
    const sk = document.getElementById("dbg-stack");
    if (sk) {
      sk.innerHTML = "";
      const frames = stack.length ? stack.slice().reverse() : [];
      frames.concat(["main"]).forEach((f) => sk.append(el("div", "dbg-frame", f + "()")));
    }
    // 高亮当前暂停的积木
    document.querySelectorAll(".block.paused-at").forEach((b) => b.classList.remove("paused-at"));
    if (node._el && node._el.isConnected) {
      node._el.classList.add("paused-at");
      node._el.scrollIntoView({ block: "center", behavior: "smooth" });
    }
    dbgBtns(true);
    setPvStatus("已暂停 ⏸", "warn");
  }
  function hidePausePanel() {
    const panel = document.getElementById("dbg-panel");
    const canvas = document.getElementById("preview-canvas");
    if (panel) panel.hidden = true;
    if (canvas) canvas.hidden = false;
    document.querySelectorAll(".block.paused-at").forEach((b) => b.classList.remove("paused-at"));
    dbgBtns(false);
  }
  function fmtVal(v) {
    if (Array.isArray(v)) return "[" + v.map(fmtVal).join(", ") + "]";
    if (v && typeof v === "object") return "{" + Object.entries(v).map(([k, x]) => k + ": " + fmtVal(x)).join(", ") + "}";
    if (typeof v === "string") return JSON.stringify(v);
    return String(v);
  }

  // ---------------- 执行高亮（运行到哪个积木就高亮哪个） ----------------
  let lastGlow = 0;
  function glowNode(node) {
    if (!node || !node._el || !node._el.isConnected) return;
    const now = performance.now();
    if (now - lastGlow < 60) return;     // 节流到 ~16fps，肉眼可见的流动高亮
    lastGlow = now;
    const elx = node._el;
    elx.classList.add("running");
    clearTimeout(elx._glowT);
    elx._glowT = setTimeout(() => elx.classList.remove("running"), 240);
  }

  // ---------------- 控制台（舞台区 + 积木/代码页共享同一份输出） ----------------
  const conSinks = () => [...document.querySelectorAll(".con-sink")];
  let conLines = [];
  function conAppend(sink, text) {
    const row = el("div", "con-line", text);
    sink.append(row); sink.scrollTop = sink.scrollHeight;
    while (sink.childElementCount > 200) sink.removeChild(sink.firstChild);
  }
  function conLog(text) {
    const t = String(text);
    conLines.push(t);
    if (conLines.length > 200) conLines = conLines.slice(-200);
    conSinks().forEach((s) => conAppend(s, t));   // 所有控制台面板同步显示
  }
  function conClear() { conLines = []; conSinks().forEach((s) => { s.innerHTML = ""; }); }
  // 积木/代码页底部的「预览 ⇄ 控制台」切页（边写边看）
  function setupDockTabs() {
    const tabs = [...document.querySelectorAll(".pv-tab")];
    const canvas = document.getElementById("preview-canvas");
    const con = document.getElementById("dock-console");
    tabs.forEach((t) => t.addEventListener("click", () => {
      tabs.forEach((x) => x.classList.toggle("active", x === t));
      const showCon = t.dataset.dock === "console";
      if (canvas) canvas.hidden = showCon;
      if (con) con.hidden = !showCon;
    }));
    const dpi = document.getElementById("dock-print-info");
    if (dpi) dpi.addEventListener("click", printBlockInfo);
    const dcc = document.getElementById("dock-con-clear");
    if (dcc) dcc.addEventListener("click", conClear);
  }
  function printBlockInfo() {
    const sp = sprite();
    conLog("— 积木信息：" + sp.name + " —");
    (sp.program || []).forEach((fn) => {
      const params = (fn.params || []).map((p) => p.name + ": " + p.type).join(", ");
      conLog("fn " + fn.name + "(" + params + ") -> " + (fn.ret || "int") + "  · " + countStmts(fn.body) + " 条语句");
    });
    conLog("共享：结构体 " + (project.structs || []).length + " · 全局 " + (project.globals || []).length);
  }
  function countStmts(list) {
    let n = 0;
    (list || []).forEach((s) => { n++; if (s.then) n += countStmts(s.then); if (s.else) n += countStmts(s.else); if (s.body) n += countStmts(s.body); });
    return n;
  }
  window._sinConsole = { log: conLog, clear: conClear, lines: () => conLines }; // 测试探针

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
  // 运行时函数的中文显示名（仅用于积木 UI；AST 与文本仍是英文，保证「AST 唯一真相」）
  const CALL_LABELS = {
    stage_init: "初始化舞台", stage_running: "舞台运行中?", frame_begin: "开始绘制",
    frame_end: "结束绘制", stage_close: "关闭舞台",
    sprite_new: "新建精灵", sprite_load: "载入造型", sprite_move_to: "移动到",
    sprite_x: "精灵x", sprite_y: "精灵y", sprite_draw: "画出精灵",
    sprite_move: "前进", sprite_turn: "右转", sprite_point: "面向", sprite_scale: "设大小",
    say: "说", draw_text: "画文字", draw_number: "画数字",
    key_down: "按下键?", key_left: "←键", key_right: "→键", key_up: "↑键",
    key_down_arrow: "↓键", key_space: "空格键",
    mouse_x: "鼠标x", mouse_y: "鼠标y", mouse_down: "按下鼠标?",
    sound_load: "载入声音", play_sound: "播放声音", play_tone: "播放音调",
    broadcast: "广播", received: "收到?",
    to_float: "转浮点", to_int: "取整",
    random_int: "随机数", screen_width: "屏幕宽", screen_height: "屏幕高", frame_index: "帧数",
    pen_clear: "清空画笔", pen_color: "设笔颜色", pen_size: "设笔粗细",
    pen_line: "画线", pen_dot: "画点", print: "打印",
  };
  const callLabel = (callee) => CALL_LABELS[callee] || callee;

  // 收集某函数内「当前作用域」可见的变量（全局 + 参数 + 所有 let + for 变量）
  function collectScope(fn) {
    const vars = [], seen = new Set();
    const add = (v) => { if (v && v.name && !seen.has(v.name)) { seen.add(v.name); vars.push(v); } };
    (project.globals || []).forEach((g) => add({ name: g.name, type: g.type, len: g.len || 0 }));
    (fn.params || []).forEach((p) => add({ name: p.name, type: p.type, len: 0 }));
    const walk = (list) => (list || []).forEach((s) => {
      if (s.block === "let") add({ name: s.name, type: s.type, len: s.len || 0 });
      if (s.block === "for") add({ name: s.var, type: "int", len: 0 });
      if (s.then) walk(s.then); if (s.else) walk(s.else); if (s.body) walk(s.body);
    });
    walk(fn.body);
    return { vars, structs: project.structs || [] };
  }
  // 变量下拉框：自动列出作用域里的变量（可按类型过滤：数组 / 结构体）
  function varSelect(scope, getName, setName, filter) {
    const sel = el("select", "var-select");
    let names = ((scope && scope.vars) || []).filter(filter || (() => true)).map((v) => v.name);
    const cur = getName();
    if (cur && !names.includes(cur)) names = [cur, ...names];
    if (!names.length) names = [cur || "x"];
    names.forEach((n) => { const o = document.createElement("option"); o.value = n; o.textContent = n; if (n === cur) o.selected = true; sel.append(o); });
    sel.addEventListener("pointerdown", (e) => e.stopPropagation());   // 不触发积木拖拽
    sel.addEventListener("change", () => { setName(sel.value); render(); });
    return sel;
  }
  const structOf = (scope, name) => {
    const v = ((scope && scope.vars) || []).find((x) => x.name === name);
    return v && (scope.structs || []).find((s) => s.name === v.type);
  };
  // 结构体字段下拉（能解析出结构体则给字段下拉，否则文本可编辑）
  function fieldSelect(node, scope) {
    const st = node.obj.block === "var" ? structOf(scope, node.obj.name) : null;
    if (st && st.fields) {
      const sel = el("select", "var-select");
      let names = st.fields.map((f) => f.name);
      if (!names.includes(node.name)) names = [node.name, ...names];
      names.forEach((n) => { const o = document.createElement("option"); o.value = n; o.textContent = n; if (n === node.name) o.selected = true; sel.append(o); });
      sel.addEventListener("pointerdown", (e) => e.stopPropagation());
      sel.addEventListener("change", () => { node.name = sel.value; render(); });
      return sel;
    }
    return field(() => node.name, (s) => { node.name = s || "f"; });
  }

  // 表达式渲染。replace(newNode) 若提供，则该元素成为可放置 reporter 的「槽位」。
  // scope 提供作用域变量，用于变量/数组/结构体下拉。
  function renderExpr(node, replace, scope) {
    let out;
    switch (node.block) {
      case "int":
        out = field(() => String(node.value),
          (s) => { const n = parseInt(s, 10); node.value = isNaN(n) ? 0 : n; }, "lit"); break;
      case "float":
        out = field(() => String(node.value),
          (s) => { const n = parseFloat(s); node.value = isNaN(n) ? 0 : n; }, "lit"); break;
      case "bool": {
        out = el("span", "pill lit", node.value ? "true" : "false");
        out.style.cursor = "pointer";
        out.addEventListener("pointerdown", (e) => e.stopPropagation());
        out.addEventListener("click", () => { node.value = !node.value; out.textContent = node.value ? "true" : "false"; refreshText(); });
        break;
      }
      case "string": {
        out = el("span", "pill lit");
        out.append(el("span", "kw", '"'), field(() => node.value, (s) => { node.value = s; }), el("span", "kw", '"'));
        break;
      }
      case "var": {
        out = el("span", "pill varref");
        // 变量下拉：自动列出作用域里的变量供选择（无作用域时退化为文本）
        if (scope) out.append(varSelect(scope, () => node.name, (v) => { node.name = v; }));
        else out.append(field(() => node.name, (s) => { node.name = s || "x"; }));
        break;
      }
      case "unary": {
        out = el("span", "pill op");
        out.append(el("span", "kw", node.op), renderExpr(node.operand, (n) => { node.operand = n; render(); }, scope));
        break;
      }
      case "binary": {
        out = el("span", "pill op");
        out.append(renderExpr(node.lhs, (n) => { node.lhs = n; render(); }, scope),
          el("span", "kw", node.op),
          renderExpr(node.rhs, (n) => { node.rhs = n; render(); }, scope));
        break;
      }
      case "call": {
        out = el("span", "pill call");
        const label = callLabel(node.callee);
        if (!node.args.length) { out.append(el("span", "kw", label)); break; }
        out.append(el("span", "kw", label + " ("));
        node.args.forEach((a, i) => { if (i) out.append(el("span", "kw", ",")); out.append(renderExpr(a, (n) => { node.args[i] = n; render(); }, scope)); });
        out.append(el("span", "kw", ")"));
        break;
      }
      case "index": {
        out = el("span", "pill varref");
        // 数组名下拉（只列数组类变量）+ 索引槽
        if (scope && node.arr.block === "var") out.append(varSelect(scope, () => node.arr.name, (v) => { node.arr.name = v; }, (x) => x.len > 0));
        else out.append(renderExpr(node.arr, (n) => { node.arr = n; render(); }, scope));
        out.append(el("span", "kw", "["), renderExpr(node.idx, (n) => { node.idx = n; render(); }, scope), el("span", "kw", "]"));
        break;
      }
      case "array": {
        out = el("span", "pill lit");
        out.append(el("span", "kw", "["));
        node.elems.forEach((a, i) => { if (i) out.append(el("span", "kw", ",")); out.append(renderExpr(a, (n) => { node.elems[i] = n; render(); }, scope)); });
        out.append(el("span", "kw", "]"));
        break;
      }
      case "field": {
        out = el("span", "pill varref");
        // 结构体变量下拉（只列结构体类变量）. 字段下拉
        if (scope && node.obj.block === "var") out.append(varSelect(scope, () => node.obj.name, (v) => { node.obj.name = v; }, (x) => structOf(scope, x.name)));
        else out.append(renderExpr(node.obj, (n) => { node.obj = n; render(); }, scope));
        out.append(el("span", "kw", "."), scope ? fieldSelect(node, scope) : el("span", null, node.name));
        break;
      }
      case "structlit": {
        out = el("span", "pill call");
        out.append(el("span", "kw", node.typeName + " {"));
        node.fields.forEach((f, i) => {
          if (i) out.append(el("span", "kw", ","));
          out.append(el("span", "kw", f.name + ":"), renderExpr(f.value, (n) => { f.value = n; render(); }, scope));
        });
        out.append(el("span", "kw", "}"));
        break;
      }
      default: out = el("span", "pill lit", "?");
    }
    if (replace) {
      out.classList.add("expr-slot");
      out._slot = { node, replace };
      out.addEventListener("contextmenu", (e) => {   // 右键把槽位重置为默认数字（撤销嵌套）
        e.preventDefault(); e.stopPropagation(); replace({ block: "int", value: 0 });
      });
    }
    return out;
  }

  function renderStmtList(list, scope) {
    const s = el("div", "stack");
    s._list = list;                 // 作为拖拽重排的落点（drop zone）
    list.forEach((st) => s.append(renderStmt(st, list, scope)));
    return s;
  }

  function renderStmt(node, list, scope) {
    let blk;
    if (node.block === "let" || node.block === "assign") {
      blk = el("div", "block var");
      const row = el("div", "hdr");
      row.append(el("span", "label", node.block === "let" ? "设" : "赋"));
      // 赋值语句的变量名也用下拉；let 声明仍是文本（在定义新变量）
      if (node.block === "assign" && scope) row.append(varSelect(scope, () => node.name, (v) => { node.name = v; }));
      else row.append(field(() => node.name, (s) => { node.name = s || "x"; }));
      if (node.block === "let") {
        const t = node.type + (node.len > 0 ? "[" + node.len + "]" : "");
        row.append(el("span", "kw", ": " + t));
      } else if (node.index) {
        row.append(el("span", "kw", "["), renderExpr(node.index, (n) => { node.index = n; render(); }, scope), el("span", "kw", "]"));
      }
      if (node.value !== undefined) row.append(el("span", "kw", "="), renderExpr(node.value, (n) => { node.value = n; render(); }, scope));
      blk.append(row);
    } else if (node.block === "if") {
      blk = el("div", "block ctrl");
      const row = el("div", "hdr");
      row.append(el("span", "label", "如果"), renderExpr(node.cond, (n) => { node.cond = n; render(); }, scope), el("span", "kw", "那么"));
      blk.append(row);
      const m = el("div", "mouth"); m.append(renderStmtList(node.then, scope)); blk.append(m);
      if (node.else) { blk.append(el("div", "hdr")); const m2 = el("div", "mouth"); m2.append(renderStmtList(node.else, scope)); blk.append(m2); }
    } else if (node.block === "while") {
      blk = el("div", "block ctrl");
      const row = el("div", "hdr");
      row.append(el("span", "label", "重复直到非"), renderExpr(node.cond, (n) => { node.cond = n; render(); }, scope));
      blk.append(row);
      const m = el("div", "mouth"); m.append(renderStmtList(node.body, scope)); blk.append(m);
    } else if (node.block === "for") {
      blk = el("div", "block ctrl");
      const row = el("div", "hdr");
      row.append(el("span", "label", "对"), field(() => node.var, (s) => { node.var = s || "i"; }),
        el("span", "kw", "从"), renderExpr(node.start, (n) => { node.start = n; render(); }, scope),
        el("span", "kw", ".."), renderExpr(node.end, (n) => { node.end = n; render(); }, scope));
      blk.append(row);
      const m = el("div", "mouth"); m.append(renderStmtList(node.body, scope)); blk.append(m);
    } else if (node.block === "return") {
      blk = el("div", "block ret");
      const row = el("div", "hdr");
      row.append(el("span", "label", "返回"));
      if (node.value) row.append(renderExpr(node.value, (n) => { node.value = n; render(); }, scope));
      blk.append(row);
    } else if (node.block === "expr") {
      blk = el("div", "block ev");
      const row = el("div", "hdr"); row.append(renderExpr(node.expr, (n) => { node.expr = n; render(); }, scope)); blk.append(row);
    } else {
      blk = el("div", "block", JSON.stringify(node));
    }
    node._el = blk;   // 供「执行高亮」按节点定位 DOM
    // 拖拽重排：按住语句积木拖动，可在各 stack（函数体 / 控制块嘴巴）间移动；拖到调色板=删除
    blk.addEventListener("pointerdown", (e) => {
      if (e.target.classList.contains("field")) return;
      if (e.altKey) { toggleBreakpoint(node, blk); return; }   // Alt+点击 = 设/清断点
      if (list) startStmtDrag({ node, fromList: list, blockEl: blk, e });
    });
    if (breakpoints.has(node)) blk.classList.add("bp");          // 重绘后保持断点标记
    // 右键删除积木
    blk.addEventListener("contextmenu", (e) => {
      if (!list) return;
      e.preventDefault(); e.stopPropagation();
      const i = list.indexOf(node); if (i >= 0) { list.splice(i, 1); render(); }
    });
    return blk;
  }

  // ---------------- 积木拖拽（语句重排 / 从调色板拖入 / 拖到调色板删除 / 表达式嵌套） ----------------
  let drag = null;
  window._sinDragState = () => (drag ? { kind: drag.kind, hasTarget: !!drag.target, hasSlot: !!drag.slot, fromList: !!drag.fromList } : null);
  function isOverPalette(ev) {
    if (!ev) return false;
    const r = document.getElementById("palette").getBoundingClientRect();
    return ev.clientX >= r.left && ev.clientX <= r.right && ev.clientY >= r.top && ev.clientY <= r.bottom;
  }
  function makeGhost(srcEl, e) {
    const r = srcEl.getBoundingClientRect();
    const ghost = srcEl.cloneNode(true);
    Object.assign(ghost.style, {
      position: "fixed", left: r.left + "px", top: r.top + "px", width: r.width + "px",
      pointerEvents: "none", opacity: ".92", zIndex: 9999, transform: "rotate(2deg)",
      boxShadow: "0 8px 20px rgba(0,0,0,.3)", margin: 0,
    });
    document.body.appendChild(ghost);
    return { ghost, offx: e.clientX - r.left, offy: e.clientY - r.top };
  }
  function markDeleteZone(ev) {
    const pal = document.getElementById("palette");
    pal.classList.toggle("delete-zone", !drag.target && !drag.slot && isOverPalette(ev));
  }

  // 语句积木拖拽：fromList=null 表示来自调色板的新积木；拖到调色板上松手 = 删除
  function startStmtDrag({ node, fromList, blockEl, srcEl, e }) {
    e.stopPropagation();
    const { ghost, offx, offy } = makeGhost(srcEl || blockEl, e);
    if (blockEl) blockEl.style.opacity = ".25";
    const indicator = el("div", "drop-indicator");
    document.body.classList.add("dragging-block");
    drag = { kind: "stmt", node, fromList, blockEl, ghost, indicator, target: null, offx, offy, lastEv: e };
    const move = (ev) => {
      drag.lastEv = ev;
      ghost.style.left = (ev.clientX - offx) + "px"; ghost.style.top = (ev.clientY - offy) + "px";
      updateDropTarget(ev); markDeleteZone(ev);
    };
    const up = () => {
      window.removeEventListener("pointermove", move); window.removeEventListener("pointerup", up);
      ghost.remove(); if (indicator.parentNode) indicator.remove();
      document.body.classList.remove("dragging-block");
      document.getElementById("palette").classList.remove("delete-zone");
      finishStmtDrop(); drag = null;
    };
    window.addEventListener("pointermove", move); window.addEventListener("pointerup", up);
  }

  function updateDropTarget(ev) {
    const stacks = [...canvas.querySelectorAll(".stack")];
    let best = null;
    for (const st of stacks) {
      if (drag.blockEl && drag.blockEl.contains(st)) continue;   // 不能放进自身子树
      const r = st.getBoundingClientRect();
      if (ev.clientX >= r.left - 14 && ev.clientX <= r.right + 14 &&
          ev.clientY >= r.top - 22 && ev.clientY <= r.bottom + 22) { best = st; break; }
    }
    [...canvas.querySelectorAll(".mouth.drop-in")].forEach((m) => m.classList.remove("drop-in"));
    if (!best) { drag.target = null; if (drag.indicator.parentNode) drag.indicator.remove(); return; }
    if (best.parentNode && best.parentNode.classList.contains("mouth")) best.parentNode.classList.add("drop-in");
    const kids = [...best.children].filter((c) => c.classList.contains("block"));
    let idx = kids.length;
    for (let i = 0; i < kids.length; i++) {
      const r = kids[i].getBoundingClientRect();
      if (ev.clientY < r.top + r.height / 2) { idx = i; break; }
    }
    best.insertBefore(drag.indicator, kids[idx] || null);
    drag.target = { list: best._list, index: idx };
  }

  function finishStmtDrop() {
    if (!drag.fromList) {                 // 来自调色板的新积木：有落点就插入，没有就丢弃
      if (drag.target) drag.target.list.splice(drag.target.index, 0, drag.node);
      render(); return;
    }
    if (!drag.target) {                   // 没落点：拖到调色板 = 删除，否则原样复位
      if (isOverPalette(drag.lastEv)) {
        const i = drag.fromList.indexOf(drag.node); if (i >= 0) drag.fromList.splice(i, 1);
      }
      render(); return;
    }
    const fromIdx = drag.fromList.indexOf(drag.node);
    if (fromIdx < 0) { render(); return; }
    drag.fromList.splice(fromIdx, 1);
    let idx = drag.target.index;
    if (drag.target.list === drag.fromList && fromIdx < idx) idx--;
    drag.target.list.splice(idx, 0, drag.node);
    render();
  }

  // 表达式积木拖拽：从调色板把「运算/侦测」reporter 拖进某个表达式槽位（嵌套）
  function startExprDrag({ node, srcEl, e }) {
    e.stopPropagation();
    const { ghost, offx, offy } = makeGhost(srcEl, e);
    document.body.classList.add("dragging-block");
    drag = { kind: "expr", node, ghost, offx, offy, slot: null, slotEl: null, lastEv: e };
    const move = (ev) => {
      drag.lastEv = ev;
      ghost.style.left = (ev.clientX - offx) + "px"; ghost.style.top = (ev.clientY - offy) + "px";
      updateExprTarget(ev);
    };
    const up = () => {
      window.removeEventListener("pointermove", move); window.removeEventListener("pointerup", up);
      ghost.remove(); document.body.classList.remove("dragging-block");
      if (drag.slotEl) drag.slotEl.classList.remove("slot-hover");
      if (drag.slot) drag.slot.replace(drag.node); else render();
      drag = null;
    };
    window.addEventListener("pointermove", move); window.addEventListener("pointerup", up);
  }
  function updateExprTarget(ev) {
    if (drag.slotEl) { drag.slotEl.classList.remove("slot-hover"); drag.slotEl = null; drag.slot = null; }
    const hit = document.elementFromPoint(ev.clientX, ev.clientY);
    const slotEl = hit && hit.closest && hit.closest(".expr-slot");
    if (slotEl && canvas.contains(slotEl)) { slotEl.classList.add("slot-hover"); drag.slotEl = slotEl; drag.slot = slotEl._slot; }
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
    if (fn.body) { const m = el("div", "mouth"); m.append(renderStmtList(fn.body, collectScope(fn))); blk.append(m); }
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
    project.imports = blk.imports || [];
    project.structs = blk.structs || [];
    project.globals = blk.globals || [];
    sprite().program = prog;
    selected = prog[0] || null;
    // 导入即得积木：import 的库函数签名变了就重建调色板的库分类
    if (rebuildLibCats(blk.libs)) buildPalette();
    project.libImpl = blk.libImpl || [];   // 库函数实现：不进画布，但预览要靠它执行
    renderCanvas(); // 不回写文本，避免打断输入
    const diags = res.diags || [];
    renderDiags(diags);
    setTextStatus(diags.length ? (diags.length + " 个问题") : "已同步 ✓", diags.length ? "warn" : "ok");
    schedulePreview(); // 文本编辑也实时刷新预览
  }
  textOut.addEventListener("input", () => {
    syncHighlight();                 // 即时高亮
    showAC();                        // 刷新补全
    clearTimeout(textTimer);
    textTimer = setTimeout(onTextEdited, 250);
  });
  textOut.addEventListener("scroll", () => { syncHighlight(); if (ac.open) positionAC(); });

  // ---------------- IDE 查询（mini-LSP）：光标类型 + F2 重命名 ----------------
  // 文本偏移 → 1-based (line, col)，与编译器 token.col 对齐
  function caretLineCol(pos) {
    const before = textOut.value.slice(0, pos);
    return { line: before.split("\n").length, col: pos - before.lastIndexOf("\n") };
  }
  function ideCall(fn, extraTypes, extraArgs) {
    if (!sincMod) return null;
    const { line, col } = caretLineCol(textOut.selectionStart);
    try {
      const out = sincMod.ccall(fn, "string",
        ["string", "number", "number"].concat(extraTypes || []),
        [textOut.value, line, col].concat(extraArgs || []));
      return JSON.parse(out);
    } catch (e) { return null; }
  }
  // 光标停在标识符上时，状态栏显示其类型（悬停显示类型的等价体验）
  function showCaretType() {
    if (!sincMod || ac.open) return;
    const h = ideCall("sin_hover");
    if (h && h.found) setTextStatus(h.kind + " " + h.name + " : " + h.type, "ok");
  }
  textOut.addEventListener("keyup", (e) => {
    if (["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(e.key)) showCaretType();
  });
  textOut.addEventListener("click", showCaretType);
  textOut.addEventListener("keydown", (e) => {
    if (e.key === "F2") {
      e.preventDefault();
      const cur = ideCall("sin_hover");
      if (!cur || !cur.found) { setTextStatus("光标处不是可改名的标识符", "warn"); return; }
      const nn = (prompt("把「" + cur.name + "」重命名为：", cur.name) || "").trim();
      if (!nn || nn === cur.name) return;
      const res = ideCall("sin_rename", ["string"], [nn]);
      if (res && res.ok) {
        setTextValue(res.source);
        onTextEdited();
        setTextStatus("已重命名 " + cur.name + " → " + nn + (res.note ? "（" + res.note + "）" : " ✓"),
                      res.note ? "warn" : "ok");
      } else {
        setTextStatus((res && res.note) || "无法重命名", "warn");
      }
    }
  });

  // ---------------- 语法诊断（点行可跳转） ----------------
  const diagList = document.getElementById("diag-list");
  function renderDiags(diags) {
    if (!diagList) return;
    if (!diags || !diags.length) { diagList.hidden = true; diagList.innerHTML = ""; return; }
    diagList.hidden = false; diagList.innerHTML = "";
    const head = el("div", "diag-head");
    head.append(el("span", null, "⚠ " + diags.length + " 个问题"));
    diagList.append(head);
    diags.slice(0, 40).forEach((d) => {
      const row = el("div", "diag-row");
      row.append(el("span", "diag-loc", "第" + d.line + "行" + (d.col ? ":" + d.col : "")));
      row.append(el("span", "diag-msg", d.msg || ""));
      row.addEventListener("click", () => jumpToLine(d.line, d.col));
      diagList.append(row);
    });
  }
  function jumpToLine(line, col) {
    const lines = textOut.value.split("\n");
    let pos = 0;
    for (let i = 0; i < line - 1 && i < lines.length; i++) pos += lines[i].length + 1;
    pos += Math.max(0, (col || 1) - 1);
    textOut.focus(); textOut.setSelectionRange(pos, pos);
    const lh = 20; textOut.scrollTop = Math.max(0, (line - 4)) * lh; syncHighlight();
  }

  // ---------------- 代码补全（单词补全 + 函数签名提示） ----------------
  const acPop = document.getElementById("ac-pop");
  const AC_KEYWORDS = ["let", "fn", "if", "else", "while", "for", "in", "return", "extern", "struct", "true", "false"];
  const AC_TYPES = ["int", "float", "bool", "string", "void"];
  const ac = { open: false, items: [], sel: 0, word: null };

  function dynamicIdents() {
    const txt = textOut.value, set = new Set();
    const res = [
      /\bfn\s+([A-Za-z_]\w*)/g, /\blet\s+([A-Za-z_]\w*)/g, /\bstruct\s+([A-Za-z_]\w*)/g,
      /\bfor\s+([A-Za-z_]\w*)\s+in/g, /([A-Za-z_]\w*)\s*:/g,
    ];
    res.forEach((re) => { let m; while ((m = re.exec(txt))) set.add(m[1]); });
    return [...set];
  }
  function completionPool() {
    const out = [];
    AC_KEYWORDS.forEach((w) => out.push({ text: w, kind: "kw", detail: "关键字" }));
    AC_TYPES.forEach((w) => out.push({ text: w, kind: "ty", detail: "类型" }));
    RUNTIME_EXTERN_DECLS.forEach(([name, decl]) => out.push({
      text: name, kind: "fn",
      detail: (CALL_LABELS[name] ? CALL_LABELS[name] + " · " : "") + decl.replace(/^extern fn\s+/, ""),
    }));
    out.push({ text: "print", kind: "fn", detail: "打印" });
    dynamicIdents().forEach((id) => out.push({ text: id, kind: "id", detail: "本项目标识符" }));
    const seen = new Set();
    return out.filter((e) => (seen.has(e.text) ? false : (seen.add(e.text), true)));
  }
  function wordAtCaret() {
    const pos = textOut.selectionStart;
    const before = textOut.value.slice(0, pos);
    const m = before.match(/[A-Za-z_]\w*$/);
    return m ? { word: m[0], start: pos - m[0].length, end: pos } : null;
  }
  function caretXY(ta, pos) {
    const div = document.createElement("div"), st = getComputedStyle(ta);
    ["fontFamily", "fontSize", "fontWeight", "lineHeight", "letterSpacing", "padding", "border", "boxSizing", "tabSize"]
      .forEach((p) => { div.style[p] = st[p]; });
    div.style.position = "absolute"; div.style.visibility = "hidden"; div.style.whiteSpace = "pre";
    div.textContent = ta.value.slice(0, pos);
    const span = document.createElement("span"); span.textContent = "​"; div.appendChild(span);
    document.body.appendChild(div);
    const x = span.offsetLeft, y = span.offsetTop;
    document.body.removeChild(div);
    const r = ta.getBoundingClientRect();
    return { left: r.left + x - ta.scrollLeft, top: r.top + y - ta.scrollTop };
  }
  function positionAC() {
    if (!ac.word) return;
    const xy = caretXY(textOut, ac.word.start);
    acPop.style.left = Math.round(xy.left) + "px";
    acPop.style.top = Math.round(xy.top + 22) + "px";
  }
  function renderAC() {
    acPop.innerHTML = "";
    ac.items.forEach((e, i) => {
      const row = el("div", "ac-item" + (i === ac.sel ? " sel" : ""));
      row.append(el("span", "ac-k ac-" + e.kind, ({ kw: "关", ty: "型", fn: "f", id: "x" })[e.kind] || "·"));
      row.append(el("span", "ac-t", e.text));
      if (e.detail) row.append(el("span", "ac-d", e.detail));
      row.addEventListener("mousedown", (ev) => { ev.preventDefault(); acceptAC(i); });
      acPop.append(row);
    });
  }
  function showAC() {
    if (document.activeElement !== textOut) return hideAC();
    const w = wordAtCaret();
    if (!w || w.word.length < 1) return hideAC();
    const ql = w.word.toLowerCase();
    const items = completionPool()
      .filter((e) => e.text !== w.word && e.text.toLowerCase().startsWith(ql))
      .sort((a, b) => a.text.length - b.text.length).slice(0, 12);
    if (!items.length) return hideAC();
    ac.open = true; ac.items = items; ac.sel = 0; ac.word = w;
    renderAC(); positionAC(); acPop.hidden = false;
  }
  function hideAC() { ac.open = false; acPop.hidden = true; }
  function acceptAC(i) {
    const e = ac.items[i != null ? i : ac.sel]; if (!e) return;
    const w = ac.word, v = textOut.value;
    textOut.value = v.slice(0, w.start) + e.text + v.slice(w.end);
    const np = w.start + e.text.length;
    textOut.setSelectionRange(np, np);
    hideAC(); syncHighlight();
    clearTimeout(textTimer); textTimer = setTimeout(onTextEdited, 250);
  }
  textOut.addEventListener("keydown", (e) => {
    if (!ac.open) {
      if (e.key === " " && e.ctrlKey) { e.preventDefault(); showAC(); }
      return;
    }
    if (e.key === "ArrowDown") { e.preventDefault(); ac.sel = (ac.sel + 1) % ac.items.length; renderAC(); }
    else if (e.key === "ArrowUp") { e.preventDefault(); ac.sel = (ac.sel - 1 + ac.items.length) % ac.items.length; renderAC(); }
    else if (e.key === "Enter" || e.key === "Tab") { e.preventDefault(); acceptAC(); }
    else if (e.key === "Escape") { e.preventDefault(); hideAC(); }
  });
  textOut.addEventListener("blur", () => setTimeout(hideAC, 150));
  window._sinAC = { show: showAC, state: ac, accept: acceptAC }; // 测试探针

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
    ["key_space", "extern fn key_space() -> int"],
    ["mouse_x", "extern fn mouse_x() -> float"],
    ["mouse_y", "extern fn mouse_y() -> float"],
    ["mouse_down", "extern fn mouse_down() -> bool"],
    ["sprite_move", "extern fn sprite_move(s: int, steps: float)"],
    ["sprite_turn", "extern fn sprite_turn(s: int, degrees: float)"],
    ["sprite_point", "extern fn sprite_point(s: int, degrees: float)"],
    ["sprite_scale", "extern fn sprite_scale(s: int, k: float)"],
    ["random_int", "extern fn random_int(lo: int, hi: int) -> int"],
    ["screen_width", "extern fn screen_width() -> int"],
    ["screen_height", "extern fn screen_height() -> int"],
    ["frame_index", "extern fn frame_index() -> int"],
    ["pen_clear", "extern fn pen_clear()"],
    ["pen_color", "extern fn pen_color(r: int, g: int, b: int)"],
    ["pen_size", "extern fn pen_size(w: float)"],
    ["pen_line", "extern fn pen_line(x1: float, y1: float, x2: float, y2: float)"],
    ["pen_dot", "extern fn pen_dot(x: float, y: float)"],
  ];
  function exportSource() {
    const model = fullModel();
    const have = new Set((model.program || []).map((f) => f.name));
    const externs = RUNTIME_EXTERN_DECLS.filter(([name]) => !have.has(name)).map(([, t]) => t);
    const head = externs.length ? "// 运行时声明（导出自动补全，使程序可独立编译）\n" + externs.join("\n") + "\n\n" : "";
    const src = modelToSource(model);
    if (src === null) throw new Error("编译器尚未加载完成，请稍候再导出");
    return head + src;
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
  window._sinBuildHTML = (name) => buildStandaloneHTML(name || "game"); // 供测试：生成单文件 HTML

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
      const htmlSel = platforms.includes("html");
      const serverPlats = platforms.filter((p) => p !== "html");
      setRows(platforms.map((p) => ({ label: platLabel(p), pending: true })));
      const rows = [];
      try {
        // 1) 单文件 HTML —— 完全在浏览器内生成，免任何外部工具链（桌面 exe 里也直接可用）
        if (htmlSel) {
          try {
            const html = await buildStandaloneHTML(name);
            const blob = new Blob([html], { type: "text/html;charset=utf-8" });
            const url = URL.createObjectURL(blob);
            // 立即下载一份，并给出「打开试玩」链接（点开即在浏览器里跑）
            const a = document.createElement("a"); a.href = url; a.download = name + ".html"; a.click();
            rows.push({ label: platLabel("html"), ok: true, href: url, linkText: "▶ 打开试玩" });
          } catch (err) {
            rows.push({ label: platLabel("html") + "：失败 " + err.message, ok: false });
          }
        }
        // 2) 其它平台（wasm/原生/安卓）—— 需本地构建服务；无服务则降级
        if (serverPlats.length) {
          try {
            const resp = await fetch("api/publish", {
              method: "POST", headers: { "Content-Type": "application/json" },
              body: JSON.stringify({ name, pkg, platforms: serverPlats, source, logo: logoDataURL, assets: collectAssets() }),
            });
            if (!resp.ok) throw new Error("HTTP " + resp.status);
            const out = await resp.json();
            (out.results || []).forEach((r) => rows.push({
              label: platLabel(r.platform) + (r.ok ? "" : "：" + (r.error || "失败")),
              ok: r.ok, href: r.artifact || null,
              linkText: r.platform === "web" ? "打开/下载" : "下载",
              download: r.platform === "web" ? null : (r.artifact ? r.artifact.split("/").pop() : null),
            }));
          } catch (e) {
            const blob = new Blob([source], { type: "text/plain" });
            const url = URL.createObjectURL(blob);
            rows.push({ label: "原生/wasm 需本地构建服务，已改为下载 .sin 源码", ok: false,
              href: url, linkText: name + ".sin", download: name + ".sin" });
          }
        }
        setRows(rows);
        statusEl.textContent = rows.every((r) => r.ok) ? "发布完成 ✓" : "部分完成（HTML 免依赖始终可用）";
      } finally { goBtn.disabled = false; }
    });
  }
  function platLabel(p) { return ({ html: "单文件 HTML（免依赖·推荐）", web: "Web (wasm)", linux: "Linux", windows: "Windows (.exe)", android: "Android (.apk)" })[p] || p; }
  // 收集预览/成品需要的造型资源（文件名 → PNG dataURL），随发布请求一起送给构建服务
  function collectAssets() {
    const out = {};
    project.sprites.forEach((sp) => (sp.costumes || []).forEach((c) => {
      if (c && c.data) { const nm = /\.png$/i.test(c.name) ? c.name : c.name + ".png"; out[nm] = imageDataToPNG(c.data); }
    }));
    return out;
  }
  function blobToDataURL(blob) {
    return new Promise((res) => { const r = new FileReader(); r.onload = () => res(r.result); r.readAsDataURL(blob); });
  }
  // 收集成品需要的全部造型：画板造型 + 程序里 sprite_load("x.png") 引用的文件
  async function gatherAssets() {
    const assets = collectAssets();
    const refs = new Set();
    const scan = (n) => {
      if (!n || typeof n !== "object") return;
      if (n.block === "call" && n.callee === "sprite_load" && n.args && n.args[0] && n.args[0].block === "string") refs.add(n.args[0].value);
      for (const k in n) { if (k[0] === "_") continue; const v = n[k]; if (Array.isArray(v)) v.forEach(scan); else if (v && typeof v === "object") scan(v); }
    };
    project.sprites.forEach((sp) => (sp.program || []).forEach(scan));
    for (const r of refs) {
      if (assets[r]) continue;
      try { const resp = await fetch("assets/" + r); if (resp.ok) assets[r] = await blobToDataURL(await resp.blob()); } catch (e) { /* 资源缺失则成品里用占位 */ }
    }
    return assets;
  }
  const escapeHtml = (s) => String(s).replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]));
  // 生成「单文件 HTML 游戏」：内联解释器 + 程序 + 共享状态 + 造型，双击即玩，免任何依赖。
  async function buildStandaloneHTML(name) {
    let interpSrc = "";
    try { interpSrc = await fetch("interp.js").then((r) => r.text()); } catch (e) { interpSrc = ""; }
    const programs = project.sprites.map((s) => s.program);
    const shared = { globals: project.globals || [], structs: project.structs || [] };
    const assets = await gatherAssets();
    // 序列化时丢弃 _el/_x/_y 等运行期字段（_el 是 DOM 引用，会循环）；< 全部转义防止闭合 </script>
    const data = JSON.stringify({ programs, shared, assets }, (k, v) => (k[0] === "_" ? undefined : v)).replace(/</g, "\\u003c");
    const boot =
      "const D=" + data + ";" +
      "const cv=document.getElementById('c');cv.focus();" +
      "const pv=new SinPreview(cv);" +
      "const am=new Map();for(const k in D.assets){const im=new Image();im.src=D.assets[k];am.set(k,im);}" +
      "pv.setAssets('',am);" +
      "cv.addEventListener('pointerdown',()=>cv.focus());" +
      "setTimeout(()=>pv.runProject(D.programs,()=>{},D.shared),150);";
    return "<!doctype html><html lang=zh><head><meta charset=utf-8>" +
      "<meta name=viewport content=\"width=device-width,initial-scale=1\">" +
      "<title>" + escapeHtml(name) + "</title>" +
      "<style>html,body{margin:0;height:100%;background:#1b2030}" +
      "#wrap{height:100%;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:10px;" +
      "font-family:-apple-system,'Microsoft YaHei',sans-serif;color:#cdd7ee}" +
      "canvas{background:#f5f5f7;border-radius:10px;box-shadow:0 12px 44px #0009;outline:none;max-width:96vw}" +
      ".tip{font-size:13px;opacity:.7}</style></head><body><div id=wrap>" +
      "<canvas id=c width=800 height=600 tabindex=0></canvas>" +
      "<div class=tip>点画面后用 ↑↓←→ / 空格 操作 · 由 Sincoding 生成（单文件 · 免依赖）</div></div>" +
      "<scr" + "ipt>" + interpSrc + "</scr" + "ipt>" +
      "<scr" + "ipt>" + boot + "</scr" + "ipt></body></html>";
  }

  // ---------------- 精灵列表（多精灵 / 多页积木） ----------------
  // 造型是否有绘制内容（非全透明），用于判断是否以造型为纹理
  function curCostume(sp) { return (sp.costumes || [])[sp._costume || 0] || null; }
  function spriteHasArt(sp, isCurrent) {
    let d = null;
    if (isCurrent && ce) {
      const c = ce.canvas; d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data;
    } else { const cc = curCostume(sp); if (cc && cc.data) d = cc.data.data; }
    if (!d) return false;
    for (let i = 3; i < d.length; i += 4) if (d[i] > 8) return true;
    return false;
  }

  function costumeThumb(sp, isCurrent) {
    if (isCurrent && ce) return ce.toDataURL();
    const cc = curCostume(sp); const data = cc && cc.data;
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
      // 当前造型名（仿 Scratch：精灵上显示其当前造型）
      const cc = (i === project.cur && ce) ? { name: ce.currentName() } : curCostume(sp);
      if (cc && cc.name) card.append(el("div", "cos-nm", "🎨 " + cc.name));
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
    sprite_move_to: () => Ex(C("sprite_move_to", Vr("s"), F(0), F(0))),
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
    // 运动（朝向）
    sprite_move: () => Ex(C("sprite_move", Vr("s"), F(10))),
    sprite_turn: () => Ex(C("sprite_turn", Vr("s"), F(15))),
    sprite_point: () => Ex(C("sprite_point", Vr("s"), F(90))),
    sprite_scale: () => Ex(C("sprite_scale", Vr("s"), F(1))),
    // 画笔
    pen_clear: () => Ex(C("pen_clear")),
    pen_color: () => Ex(C("pen_color", I(255), I(0), I(0))),
    pen_size: () => Ex(C("pen_size", F(2))),
    pen_line: () => Ex(C("pen_line", F(0), F(0), F(100), F(100))),
    pen_dot: () => Ex(C("pen_dot", F(0), F(0))),
    // 平台 API
    if_mouse: () => ({ block: "if", cond: C("mouse_down"), then: [] }),
    let_mouse_x: () => ({ block: "let", name: "mx", type: "float", len: 0, value: C("mouse_x") }),
    let_mouse_y: () => ({ block: "let", name: "my", type: "float", len: 0, value: C("mouse_y") }),
    let_random: () => ({ block: "let", name: "r", type: "int", len: 0, value: C("random_int", I(1), I(10)) }),
    let_screen_w: () => ({ block: "let", name: "w", type: "int", len: 0, value: C("screen_width") }),
    repeat: () => ({ block: "for", var: "i", start: { block: "int", value: 0 }, end: { block: "int", value: 10 }, body: [] }),
  };
  // reporter（表达式）积木：从调色板拖进某个表达式槽位即可嵌套（运算 / 比较 / 字符串 / 侦测）
  const B2 = (op) => () => Bn(op, I(0), I(0));
  const Bb = (op) => () => Bn(op, { block: "bool", value: true }, { block: "bool", value: true });
  const REPORTERS = {
    r_add: B2("+"), r_sub: B2("-"), r_mul: B2("*"),
    r_div: () => Bn("/", I(0), I(1)), r_mod: () => Bn("%", I(0), I(1)),
    r_lt: B2("<"), r_gt: B2(">"), r_eq: B2("=="), r_le: B2("<="), r_ge: B2(">="), r_ne: B2("!="),
    r_and: Bb("&&"), r_or: Bb("||"), r_not: () => ({ block: "unary", op: "!", operand: { block: "bool", value: true } }),
    r_int: () => I(0), r_float: () => F(0), r_str: () => S("文字"),
    r_true: () => ({ block: "bool", value: true }), r_var: () => Vr("x"),
    r_index: () => ({ block: "index", arr: Vr("a"), idx: I(0) }),       // 数组元素 a[i]
    r_field: () => ({ block: "field", obj: Vr("s"), name: "field" }),   // 结构体字段 s.field
    r_random: () => C("random_int", I(1), I(10)),
    r_mouse_x: () => C("mouse_x"), r_mouse_y: () => C("mouse_y"), r_mouse_down: () => C("mouse_down"),
    r_key: () => C("key_down", C("key_left")), r_received: () => C("received", S("go")),
    r_sprite_x: () => C("sprite_x", Vr("s")), r_sprite_y: () => C("sprite_y", Vr("s")),
  };
  function addStmt(kind) {
    if (!selected || !selected.body) return;
    selected.body.push(NEW[kind]()); render();
  }
  // 直接加入一个现成节点（库积木按签名生成，没有固定 kind）
  function addStmtNode(node) {
    if (!selected || !selected.body) return;
    selected.body.push(node); render();
  }
  function addFn() {
    const fn = { block: "fn", name: "fn" + (sprite().program.length + 1), params: [], ret: "int",
      body: [{ block: "return", value: { block: "int", value: 0 } }],
      _x: 40 - view.x / view.k + 60, _y: 40 - view.y / view.k + 60 };
    sprite().program.push(fn); selected = fn; render();
  }

  // 调色板分类（仿 Scratch：左侧分类导航 + 右侧「所见即所得」积木预览）
  const PALETTE = [
    { id: "custom", name: "自制积木", color: "#FF6680", items: [{ special: "addFn", label: "新建函数" }] },
    { id: "data", name: "变量 / 数据", color: "#FF8C1A", items: ["let", "let_str", "let_arr", "set_idx"] },
    { id: "op", name: "运算", color: "#59C059", items: ["incr", "decr", "set_op", "to_int", "to_float"] },
    { id: "reporters", name: "运算块 (拖入槽)", color: "#59C059", reporter: true,
      items: ["r_var", "r_index", "r_field", "r_add", "r_sub", "r_mul", "r_div", "r_mod",
        "r_lt", "r_gt", "r_eq", "r_le", "r_ge", "r_ne", "r_and", "r_or", "r_not",
        "r_int", "r_float", "r_str", "r_true", "r_random",
        "r_mouse_x", "r_mouse_y", "r_mouse_down", "r_key", "r_received", "r_sprite_x", "r_sprite_y"] },
    { id: "control", name: "控制", color: "#FFAB19", items: ["if", "if_else", "while", "for", "repeat", "return", "print"] },
    { id: "stage", name: "舞台", color: "#FFAB19", items: ["stage_init", "game_loop", "frame_begin", "frame_end", "stage_close"] },
    { id: "motion", name: "运动", color: "#4C97FF", items: ["sprite_new", "sprite_move_to", "sprite_move", "sprite_turn", "sprite_point", "sprite_scale", "sprite_x", "sprite_y"] },
    { id: "looks", name: "外观", color: "#9966FF", items: ["sprite_load", "sprite_draw", "say", "draw_text", "draw_number"] },
    { id: "pen", name: "画笔", color: "#0FBD8C", items: ["pen_clear", "pen_color", "pen_size", "pen_line", "pen_dot"] },
    { id: "platform", name: "平台", color: "#5CB1D6", items: ["if_mouse", "let_mouse_x", "let_mouse_y", "let_random", "let_screen_w"] },
    { id: "events", name: "事件 / 声音", color: "#FFBF00", items: ["if_key", "if_key_right", "if_key_up", "if_key_down", "broadcast", "if_received", "play_tone", "play_sound"] },
    { id: "modules", name: "模块 / 库", color: "#CF63CF",
      items: [{ special: "addImport", label: "导入库…" }] },
  ];

  // 内置标准库清单（供「导入库」快捷选择；也可手输任意模块名）
  const STD_MODULES = ["std/mathx", "std/arrayx"];
  function addImport() {
    const cur = (project.imports || []).join("、") || "（无）";
    const name = (prompt("导入哪个库？\n内置：" + STD_MODULES.join(" / ") +
                         "\n（也可填同目录下的 .sin 文件名）\n\n已导入：" + cur,
                         STD_MODULES[1]) || "").trim();
    if (!name) return;
    project.imports = project.imports || [];
    if (project.imports.includes(name)) { setTextStatus("已经导入过 " + name, "warn"); return; }
    project.imports.push(name);
    refreshText();     // 写回文本 → 触发重新解析 → 库积木自动出现在调色板
    onTextEdited();
  }

  // ---- 导入即得积木：由 import 的库函数**签名**自动生成分类 ----
  // 参数类型决定槽位默认值，返回类型决定是语句块（void）还是 reporter（有返回值）。
  // 只读签名，不读任何外观元数据——签名一变，积木自动跟着变。
  const LIB_CATS = [];                 // 动态追加到 PALETTE 之后
  const LIB_COLORS = ["#CF63CF", "#0FBD8C", "#5CB1D6", "#FF8C1A"];
  function libSlot(p) {                // 依参数类型给一个合理的默认实参
    if (p.len !== 0) return { block: "var", name: p.name };   // 数组/切片：变量槽
    switch (p.type) {
      case "float": return { block: "float", value: 0 };
      case "bool": return { block: "bool", value: true };
      case "string": return { block: "string", value: "" };
      case "int": return { block: "int", value: 0 };
      default: return { block: "var", name: p.name };          // 结构体等：变量槽
    }
  }
  function libNode(f) {
    const call = { block: "call", callee: f.name, args: (f.params || []).map(libSlot) };
    return f.ret === "void" ? { block: "expr", expr: call } : call;
  }
  // 依据 blocks JSON 的 libs 段重建库分类
  function rebuildLibCats(libs) {
    const before = JSON.stringify(LIB_CATS.map((c) => c.id + ":" + c.items.length));
    LIB_CATS.length = 0;
    const byMod = new Map();
    (libs || []).forEach((f) => {
      if (!byMod.has(f.module)) byMod.set(f.module, []);
      byMod.get(f.module).push(f);
    });
    let ci = 0;
    byMod.forEach((fns, mod) => {
      const id = "lib_" + mod.replace(/[^A-Za-z0-9]/g, "_");
      // 有返回值的做成 reporter（拖进槽位），void 的做成语句块
      const stmts = fns.filter((f) => f.ret === "void");
      const reps = fns.filter((f) => f.ret !== "void");
      const color = LIB_COLORS[ci++ % LIB_COLORS.length];
      if (stmts.length)
        LIB_CATS.push({ id: id, name: "📦 " + mod, color: color,
                        items: stmts.map((f) => ({ lib: f })) });
      if (reps.length)
        LIB_CATS.push({ id: id + "_r", name: "📦 " + mod + " (取值)", color: color, reporter: true,
                        items: reps.map((f) => ({ lib: f })) });
    });
    return JSON.stringify(LIB_CATS.map((c) => c.id + ":" + c.items.length)) !== before;
  }
  // 自制积木的预览（函数定义帽子块外观）
  function specialPreview(sp) {
    if (sp.special === "addImport") {          // 导入库：显示当前已导入的模块
      const blk = el("div", "block");
      const row = el("div", "hdr");
      row.append(el("span", "label", "导入"), el("span", "param", "库…"));
      blk.append(row);
      (project.imports || []).forEach((m) => {
        const r = el("div", "hdr");
        r.append(el("span", "kw", "已导入"), el("span", "param", m));
        blk.append(r);
      });
      return blk;
    }
    if (sp.special === "addFn") {
      const blk = el("div", "block fn hat");
      const row = el("div", "hdr");
      row.append(el("span", "label", "定义"), el("span", "param", "新函数"), el("span", "kw", "→ int"));
      blk.append(row);
      return blk;
    }
    return el("div", "block", sp.label || "");
  }

  // hover 预览：鼠标浮到某积木上，在原位弹出**完整**积木（不被调色板宽度截断）
  function palHoverEl() {
    let h = document.getElementById("pal-hover");
    if (!h) { h = el("div", "pal-hover"); h.id = "pal-hover"; h.hidden = true; document.body.appendChild(h); }
    return h;
  }
  function showPalHover(wys) {
    const blk = wys.firstElementChild; if (!blk) return;
    const h = palHoverEl();
    h.innerHTML = ""; h.appendChild(blk.cloneNode(true));
    const r = wys.getBoundingClientRect();
    h.style.left = r.left + "px"; h.style.top = r.top + "px";
    h.hidden = false;
  }
  function hidePalHover() { const h = document.getElementById("pal-hover"); if (h) h.hidden = true; }

  function buildPalette() {
    const pal = document.getElementById("palette");
    pal.innerHTML = "";
    const rail = el("div", "cat-rail");
    const list = el("div", "block-list");
    pal.append(rail, list);

    const railBtns = {};
    function setActiveCat(id) {
      Object.entries(railBtns).forEach(([k, b]) => b.classList.toggle("active", k === id));
    }

    PALETTE.concat(LIB_CATS).forEach((cat) => {
      // 左侧分类导航按钮（彩色圆点 + 名称）
      const rb = el("button", "cat-btn");
      const dot = el("span", "cat-dot"); dot.style.background = cat.color;
      rb.append(dot, el("span", "cat-nm", cat.name));
      rb.addEventListener("click", () => {
        const sec = document.getElementById("cat-" + cat.id);
        if (sec) list.scrollTo({ top: sec.offsetTop - 6, behavior: "smooth" });
        setActiveCat(cat.id);
      });
      railBtns[cat.id] = rb; rail.append(rb);

      // 右侧分类区：所见即所得的积木预览（与画布上完全一致）
      const sec = el("div", "cat-sec"); sec.id = "cat-" + cat.id;
      const h = el("h2", null, cat.name); h.style.setProperty("--cc", cat.color); sec.append(h);
      const isReporter = cat.reporter === true;
      cat.items.forEach((it) => {
        const wys = el("div", "pal-wys" + (isReporter ? " pal-reporter" : ""));
        const special = typeof it === "object" && it.special;
        const lib = typeof it === "object" && it.lib;     // 库函数：按签名生成积木
        wys.dataset.kind = special ? it.special : (lib ? "lib:" + it.lib.name : it);
        const mk = lib ? () => libNode(it.lib) : null;    // 每次取用都新建一份节点
        let preview;
        if (special) preview = specialPreview(it);
        else if (lib) {
          if (isReporter) { preview = el("span", "expr-wrap"); preview.append(renderExpr(mk())); }
          else preview = renderStmt(mk(), null);
        }
        else if (isReporter) { preview = el("span", "expr-wrap"); preview.append(renderExpr(REPORTERS[it]())); }
        else preview = renderStmt(NEW[it](), null);   // 与画布同款外观
        wys.append(preview);

        // 按下并拖动 = 拖入画布/槽位；原地松手 = 点击加入
        wys.addEventListener("pointerdown", (e) => {
          if (e.button !== 0) return;
          e.preventDefault(); hidePalHover();
          const sx = e.clientX, sy = e.clientY; let started = false;
          const mv = (ev) => {
            if (started || Math.hypot(ev.clientX - sx, ev.clientY - sy) <= 5) return;
            started = true;
            window.removeEventListener("pointermove", mv); window.removeEventListener("pointerup", up);
            if (special) return;
            const node = lib ? mk() : (isReporter ? REPORTERS[it]() : NEW[it]());
            if (isReporter) startExprDrag({ node: node, srcEl: preview, e: ev });
            else startStmtDrag({ node: node, fromList: null, blockEl: null, srcEl: preview, e: ev });
          };
          const up = () => {
            window.removeEventListener("pointermove", mv); window.removeEventListener("pointerup", up);
            if (started) return;
            if (special) { if (it.special === "addFn") addFn(); else if (it.special === "addImport") addImport(); }
            else if (lib) { if (!isReporter) addStmtNode(mk()); }  // reporter 只能拖入槽位
            else if (!isReporter) addStmt(it);
          };
          window.addEventListener("pointermove", mv); window.addEventListener("pointerup", up);
        });
        wys.addEventListener("mouseenter", () => showPalHover(wys));  // 悬停显示完整积木
        wys.addEventListener("mouseleave", hidePalHover);
        sec.append(wys);
      });
      list.append(sec);
    });
    setActiveCat(PALETTE[0].id);
    // 滚动联动：滚到哪个分类，导航就高亮哪个
    list.addEventListener("scroll", () => {
      hidePalHover();
      let cur = PALETTE[0].id;
      for (const cat of PALETTE) {
        const sec = document.getElementById("cat-" + cat.id);
        if (sec && sec.offsetTop - 12 <= list.scrollTop) cur = cat.id;
      }
      setActiveCat(cur);
    });
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
      // 造型增删改/切换 → 记录当前精灵的当前造型，并刷新舞台/精灵栏/预览
      onChange: () => {
        if (!ce) return;   // 构造期 addCostume 会触发，此时 ce 尚未赋值
        if (project.sprites[project.cur]) sprite()._costume = ce.current;
        renderSpriteBar(); renderStage(); schedulePreview();
      },
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
  setupDockTabs();
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
  const conInfo = document.getElementById("con-print-info");
  if (conInfo) conInfo.addEventListener("click", printBlockInfo);
  const conClr = document.getElementById("con-clear");
  if (conClr) conClr.addEventListener("click", conClear);
  applyView();
  renderSpriteBar();
  render();
  window._sin = { project, sprite, refreshText, render, selectSprite, addSprite }; // 测试探针
})();
