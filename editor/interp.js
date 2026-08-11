// interp.js — 积木模型解释器 + 实时预览
//
// 直接在浏览器里「跑」积木模型（不经编译）：解释表达式/语句/函数，并把
// 运行时内建（精灵/移动/文字/按键/广播/声音）画到预览画布上。游戏主循环
// while stage_running() {...} 被特殊处理为 requestAnimationFrame 逐帧执行，
// 从而不阻塞页面。编辑积木/文本即可重跑（类 Vue 实时预览）。
(function (root) {
  "use strict";

  // 键名 → raylib 键码。方向键/空格/回车显式列出；字母 A-Z(65-90) 与
  // 数字 0-9(48-57) 的码值与 raylib 一致，按需换算（成品里 key_down(65) 就是 A）
  const KEYMAP = { ArrowLeft: 263, ArrowRight: 262, ArrowUp: 265, ArrowDown: 264, " ": 32, Enter: 257 };
  function keyCode(k) {
    if (KEYMAP[k] !== undefined) return KEYMAP[k];
    if (typeof k === "string" && k.length === 1) {
      const u = k.toUpperCase().charCodeAt(0);
      if ((u >= 65 && u <= 90) || (u >= 48 && u <= 57)) return u;   // 字母 / 数字
    }
    return undefined;
  }

  // %g 对齐：C 的 print/str 对浮点用 %g（6 位有效数字、去尾零）。
  // JS 的 String(1/3) 会给 16 位，预览必须与成品打印一致。
  function fmtNum(v) {
    if (typeof v !== "number" || Number.isInteger(v) || !isFinite(v)) return String(v);
    return String(parseFloat(v.toPrecision(6)));
  }

  class ReturnSignal { constructor(v) { this.value = v; } }

  // 聚合值语义：数组/结构体在 let / 赋值 / 传参时按值拷贝（与生成的 C 一致）。
  // 切片（声明 len === -1）是刻意的借用视图，不拷贝。
  function deepVal(v) {
    if (Array.isArray(v)) return v.map(deepVal);
    if (v && typeof v === "object") { const o = {}; for (const k in v) o[k] = deepVal(v[k]); return o; }
    return v;
  }
  // 数组越界：与生成的 C 的运行时检查同语义（报行号，终止运行）
  function checkIndex(arr, ix, line) {
    if (!Array.isArray(arr)) return ix;
    if (ix < 0 || ix >= arr.length)
      throw new Error("第" + (line || "?") + "行: 数组下标 " + ix + " 越界（长度 " + arr.length + "）");
    return ix;
  }

  class SinPreview {
    constructor(canvas) {
      this.canvas = canvas;
      this.ctx = canvas.getContext("2d");
      this.keys = new Set();
      this.raf = 0;
      this.audio = null;
      // 造型资源：sprite_load(name) 渲染的就是这里的真实图像（与导出成品同源）。
      // assets: name → 可绘制对象（painted 造型用离屏 canvas，文件用 Image）；
      // assetBase: 文件名相对前缀（如 "assets/"），用于按文件名惰性加载 PNG。
      this.assets = new Map();
      this.assetBase = "";
      // 运行可观测：onStep(node) 用于「执行到哪个积木就高亮」；onPrint(text) 输出到控制台
      this.onStep = null;
      // ---- 调试器状态（M5：断点 / 单步 / 变量面板 / 调用栈）----
      this.breakpoints = new Set();   // 断点：积木节点对象（引用相等）
      this.stepMode = false;          // 单步：下一条语句就停
      this.paused = false;
      this.stack = [];                // 调用栈（函数名）
      this.onPause = null;            // (node, env, stack) => void
      this.pending = null;            // 暂停处的现场：{ai, gen}
      this.onPrint = null;
      canvas.tabIndex = 0;
      canvas.addEventListener("keydown", (e) => {
        const c = keyCode(e.key);
        if (c !== undefined) {
          if (!this.keys.has(c)) this.pressedQueue.add(c);   // 只有"从松到按"才算刚按下
          this.keys.add(c); e.preventDefault();
        }
      });
      canvas.addEventListener("keyup", (e) => {
        const c = keyCode(e.key); if (c !== undefined) this.keys.delete(c);
      });
      // 边沿检测："本帧刚按下"的键（keydown 入 pressedQueue，帧首搬到 pressedNow）
      this.pressedQueue = new Set();
      this.pressedNow = new Set();
      this.clickQueue = false; this.clickNow = false;
      // 鼠标：记录舞台坐标（中心原点、y 向上）与按下状态
      this.mouse = { x: 0, y: 0, down: false };
      canvas.addEventListener("mousemove", (e) => {
        const r = canvas.getBoundingClientRect();
        const cx = (e.clientX - r.left) * (canvas.width / r.width);
        const cy = (e.clientY - r.top) * (canvas.height / r.height);
        this.mouse.x = cx - canvas.width / 2;
        this.mouse.y = canvas.height / 2 - cy;
      });
      canvas.addEventListener("mousedown", () => { this.mouse.down = true; this.clickQueue = true; });
      window.addEventListener("mouseup", () => { this.mouse.down = false; });
    }

    stop() { if (this.raf) cancelAnimationFrame(this.raf); this.raf = 0; if (this.world) this.world.running = false; }

    beep(freq, ms) {
      try {
        if (!this.audio) this.audio = new (window.AudioContext || window.webkitAudioContext)();
        const o = this.audio.createOscillator(), g = this.audio.createGain();
        o.frequency.value = freq || 440; o.type = "square";
        g.gain.value = 0.05; o.connect(g); g.connect(this.audio.destination);
        o.start(); o.stop(this.audio.currentTime + (ms || 120) / 1000);
      } catch (e) { /* 预览无声不致命 */ }
    }

    // ---- 运行 ----
    run(program, onStatus) { this.runProject([program], onStatus); }
    runFull(model, onStatus) {
      this.runProject([model.program], onStatus, { globals: model.globals, structs: model.structs });
    }

    // 注入造型资源：base = 文件名前缀；map = name→可绘制对象（painted 造型）。
    // 这样 sprite_load("x.png") 优先用同名 painted 造型，否则按 base+name 取文件——
    // 与导出成品 sprite_load 同一来源，保证「预览所见 = 成品所见」。
    setAssets(base, map) {
      this.assetBase = base || "";
      this.assets = map instanceof Map ? map : new Map(Object.entries(map || {}));
    }

    // 把一个造型名解析为可绘制对象（canvas 同步可画；Image 加载完成后可画）
    resolveTex(name) {
      if (this.assets.has(name)) return this.assets.get(name);
      const img = new Image();
      img.src = this.assetBase + name;
      this.assets.set(name, img);   // 缓存，避免重复请求
      return img;
    }

    // 多精灵并行：每个精灵程序是一个 actor，共享同一个舞台/世界；
    // 各自的 while stage_running() 主循环每帧执行一次，协调清屏（每帧只清一次）。
    // shared = { globals: [...], structs: [...] }：项目级共享状态——所有精灵共用
    // 同一组结构体定义与全局变量（数组/结构体/标量），实现跨精灵的数据共享。
    runProject(programs, onStatus, shared) {
      this.stop();
      this.onStatus = onStatus || (() => {});
      const w = this.canvas.width, h = this.canvas.height;
      this.ctx.fillStyle = "#f5f5f7"; this.ctx.fillRect(0, 0, w, h);
      this.world = {
        W: w, H: h, running: true, frame: 0, frameCleared: false, sprites: [],
        keys: this.keys, broadcasts: new Set(), nextBroadcasts: new Set(),
        soundCount: 0, console: [],
      };
      // 画笔持久层：与主画布同尺寸的离屏画布，跨帧保留，每帧贴回主画布
      this.penCanvas = document.createElement("canvas");
      this.penCanvas.width = w; this.penCanvas.height = h;
      this.penCtx = this.penCanvas.getContext("2d");
      this.penColor = "#000"; this.penSize = 2;
      this.timerBase = performance.now();
      // 共享全局作用域：所有精灵 actor 的 env 栈底都是同一个 Map，
      // 因此一个精灵对全局数组/结构体/变量的修改对其它精灵立即可见。
      this.structDefs = {};
      (shared && shared.structs || []).forEach((s) => { this.structDefs[s.name] = s; });
      this.globalScope = new Map();
      this.fns = {};
      (shared && shared.globals || []).forEach((g) => {
        this.globalScope.set(g.name, g.value !== undefined ? this.runToEnd(this.eval(g.value, [this.globalScope])) : this.defaultVal(g));
      });
      // import 进来的库函数实现：不在画布上，但预览要靠它们执行库调用。
      // （泛型模板原样可用——JS 是动态类型的，一份模板服务所有元素类型。）
      const libFns = {};
      (shared && shared.libImpl || []).forEach((f) => { if (f && f.name) libFns[f.name] = f; });
      this.actors = [];
      let hasMain = false;
      for (const program of (programs || [])) {
        const fns = Object.assign({}, libFns);
        (program || []).forEach((f) => { if (f.block === "fn") fns[f.name] = f; });
        let main = fns["main"];
        // 事件驱动：没写 main 但定义了 on_start/on_frame/on_key_* → 合成主循环，
        // 顺序与 codegen 生成的 C 驱动完全一致（预览 = 成品）
        if (!main && (program || []).some((f) => f.block === "fn" && f.name.startsWith("on_"))) {
          const call = (name, args) => ({ block: "expr", expr: { block: "call", callee: name, args: args || [] } });
          const EV = [["on_key_space", "key_pressed_space"], ["on_key_left", "key_pressed_left"],
                      ["on_key_right", "key_pressed_right"], ["on_key_up", "key_pressed_up"],
                      ["on_key_down", "key_pressed_down"], ["on_click", "mouse_clicked"]];
          const loopBody = [call("frame_begin")];
          for (const [h, pressed] of EV)
            if (fns[h]) loopBody.push({ block: "if",
              cond: { block: "call", callee: pressed, args: [] }, then: [call(h)] });
          if (fns["on_frame"]) loopBody.push(call("on_frame"));
          loopBody.push(call("frame_end"));
          const body = [call("stage_init", [{ block: "int", value: 480 }, { block: "int", value: 360 }])];
          if (fns["on_start"]) body.push(call("on_start"));
          body.push({ block: "while", cond: { block: "call", callee: "stage_running", args: [] }, body: loopBody });
          body.push(call("stage_close"));
          main = { block: "fn", name: "main", params: [], ret: "void", body };
          fns["main"] = main;
        }
        if (!main) continue;
        hasMain = true;
        const env = [this.globalScope, new Map()];
        const body = main.body || [];
        const loopIdx = body.findIndex((s) => s.block === "while" && s.cond &&
          s.cond.block === "call" && s.cond.callee === "stage_running");
        this.fns = fns;
        try {
          if (loopIdx < 0) {
            // 无主循环：也作为 actor 走 pump 驱动的生成器——断点/单步在这类程序里
            // 同样生效（此前直接 runToEnd 跑完，断点被无声忽略，用户以为调试器坏了）
            this.actors.push({ fns, env, once: this.execList(body, env) });
          } else {
            for (let i = 0; i < loopIdx; i++) this.runToEnd(this.execStmt(body[i], env));  // 初始化
            this.actors.push({ fns, env, loop: body[loopIdx], post: body.slice(loopIdx + 1) });
          }
        } catch (e) {
          if (!(e instanceof ReturnSignal)) this.onStatus("运行出错: " + e.message, "warn");
        }
      }
      if (!hasMain) { this.onStatus("没有 main() 或事件函数（当绿旗被点击…），无法预览", "warn"); return; }
      this.hadLoop = this.actors.some((a) => a.loop);
      if (this.actors.length === 0) { this.drawConsole(); this.onStatus("运行完成 ✓", "ok"); return; }
      this.onStatus(this.actors.length > 1 ?
        (this.actors.length + " 个精灵并行运行") : "运行中（点画面用方向键/空格）", "ok");
      this.frameLoop();
    }

    frameLoop() {
      const wd = this.world;
      if (!wd || !wd.running) {
        (this.actors || []).forEach((a) => { this.fns = a.fns; try { for (const s of a.post) this.runToEnd(this.execStmt(s, a.env)); } catch (e) {} });
        if (wd) this.onStatus("已结束", "ok");
        return;
      }
      if (this.paused) return;                       // 调试暂停中：不推进
      // 从上次暂停处续跑；否则开新的一帧
      let startIdx = 0;
      if (this.pending) { startIdx = this.pending.ai; }
      else {
        wd.frameCleared = false; wd.broadcasts = wd.nextBroadcasts; wd.nextBroadcasts = new Set();
        this.pressedNow = this.pressedQueue; this.pressedQueue = new Set();   // 边沿：本帧可见
        this.clickNow = this.clickQueue; this.clickQueue = false;
      }
      for (let ai = startIdx; ai < this.actors.length; ai++) {
        const a = this.actors[ai];
        this.fns = a.fns;
        // 一次性程序（无主循环）续用同一个生成器；循环程序每帧新建循环体生成器
        const gen = a.once ? a.once
                  : (this.pending && this.pending.ai === ai) ? this.pending.gen
                                                             : this.execList(a.loop.body, a.env);
        this.pending = null;
        try {
          if (!this.pump(gen, ai)) return;           // 命中断点/单步 → 挂起，等用户操作
          if (a.once) a.done = true;                 // 一次性程序跑完即退场
        } catch (e) {
          if (e instanceof ReturnSignal) a.done = true;
          else { this.onStatus("运行出错: " + e.message, "warn"); return; }
        }
      }
      this.actors = this.actors.filter((a) => !a.done);
      if (this.actors.length === 0 && !this.hadLoop) {
        // 纯一次性程序全部跑完：画出控制台输出并收尾（与旧的同步路径同款结果）
        this.drawConsole(); this.onStatus("运行完成 ✓", "ok"); return;
      }
      if (this.actors.length === 0) wd.running = false;
      wd.frame++;
      this.raf = requestAnimationFrame(() => this.frameLoop());
    }

    // ---- 语句（生成器：每条语句 yield 一次，便于断点/单步真正挂起）----
    *execList(list, env) {
      env.push(new Map());
      try { for (const s of list) yield* this.execStmt(s, env); }
      finally { env.pop(); }
    }

    *execStmt(node, env) {
      const w = this.world;
      yield { node: node, env: env };       // 暂停点：驱动方决定继续还是停下
      switch (node.block) {
        case "let": {
          let v = node.value !== undefined ? yield* this.eval(node.value, env) : this.defaultVal(node);
          if (node.len !== -1) v = deepVal(v);   // 切片是借用视图，别的聚合按值拷贝
          env[env.length - 1].set(node.name, v);
          break;
        }
        case "assign": {
          if (node.field) { const o = this.lookup(env, node.name); if (o) o[node.field] = deepVal(yield* this.eval(node.value, env)); }
          else if (node.index) {
            const a = this.lookup(env, node.name); const ix = yield* this.eval(node.index, env);
            a[checkIndex(a, ix, node.line)] = deepVal(yield* this.eval(node.value, env));
          }
          else this.setVar(env, node.name, deepVal(yield* this.eval(node.value, env)));
          break;
        }
        case "if":
          if (yield* this.eval(node.cond, env)) yield* this.execList(node.then, env);
          else if (node.else) yield* this.execList(node.else, env);
          break;
        case "while": {
          let g = 0;
          while (yield* this.eval(node.cond, env)) {
            yield* this.execList(node.body, env);
            if (++g > 2000000) throw new Error("循环次数过多");
          }
          break;
        }
        case "for": {
          const a = yield* this.eval(node.start, env), b = yield* this.eval(node.end, env);
          env.push(new Map());
          try { for (let i = a; i < b; i++) { env[env.length - 1].set(node.var, i); yield* this.execList(node.body, env); } }
          finally { env.pop(); }
          break;
        }
        case "return":
          throw new ReturnSignal(node.value !== undefined ? yield* this.eval(node.value, env) : 0);
        case "expr":
          yield* this.eval(node.expr, env);
          break;
      }
    }

    defaultVal(node) {
      if (node.len === -2) return [];        // 动态列表：空起步
      if (node.len > 0) return new Array(node.len).fill(node.type === "float" ? 0 : (node.type === "bool" ? false : (node.type === "string" ? "" : 0)));
      if (node.type === "bool") return false;
      if (node.type === "string") return "";
      if (node.type === "float" || node.type === "int") return 0;
      return {}; // 结构体零初始化
    }

    // ---- 表达式 ----
    *eval(node, env) {
      switch (node.block) {
        case "int": case "float": case "bool": return node.value;
        case "string": return node.value;
        case "var": return this.lookup(env, node.name);
        case "unary": { const v = yield* this.eval(node.operand, env); return node.op === "-" ? -v : !v; }
        case "binary": {
          // && / || 必须短路（与 C 一致）：j >= 0 && xs[j] > k 这类写法
          // 依赖右侧不被求值——否则越界检查会在守卫本该拦住的地方误报
          const l = yield* this.eval(node.lhs, env);
          if (node.op === "&&" && !l) return false;
          if (node.op === "||" && l) return true;
          const r = yield* this.eval(node.rhs, env);
          return this.binop(node.op, l, r, node);
        }
        case "call": {
          const args = [];
          for (const a of node.args) args.push(yield* this.eval(a, env));
          return yield* this.callFn(node.callee, args);
        }
        case "index": { const a = yield* this.eval(node.arr, env); const i = yield* this.eval(node.idx, env); return a[checkIndex(a, i, node.line)]; }
        case "array": {
          const out = [];
          for (const e of node.elems) out.push(yield* this.eval(e, env));
          return out;
        }
        case "field": { const o = yield* this.eval(node.obj, env); const v = o ? o[node.name] : undefined; return v === undefined ? 0 : v; }
        case "structlit": {
          const o = {};
          for (const f of node.fields) o[f.name] = yield* this.eval(f.value, env);
          return o;
        }
      }
      return 0;
    }

    binop(op, l, r, node) {
      switch (op) {
        case "+": return l + r; case "-": return l - r; case "*": return l * r;
        case "/":
          // 浮点除法（引擎标记 ft）：真除；整数除法：除零报错（与生成的 C 同语义）+ 截断
          if (node && node.ft) return l / r;
          if (Number.isInteger(l) && Number.isInteger(r)) {
            if (r === 0) throw new Error("第" + ((node && node.line) || "?") + "行: 除数为 0");
            return Math.trunc(l / r);
          }
          return l / r;
        case "%":
          if (Number.isInteger(l) && Number.isInteger(r) && r === 0)
            throw new Error("第" + ((node && node.line) || "?") + "行: 除数为 0");
          return l % r;
        case "==": return l === r; case "!=": return l !== r;
        case "<": return l < r; case "<=": return l <= r; case ">": return l > r; case ">=": return l >= r;
        case "&&": return l && r; case "||": return l || r;
      }
      return 0;
    }

    lookup(env, name) { for (let i = env.length - 1; i >= 0; i--) if (env[i].has(name)) return env[i].get(name); return 0; }
    setVar(env, name, v) { for (let i = env.length - 1; i >= 0; i--) if (env[i].has(name)) { env[i].set(name, v); return; } env[env.length - 1].set(name, v); }

    *callFn(name, args) {
      const b = this.BUILTINS[name];
      if (b) return b.call(this, args);
      const fn = this.fns[name];
      if (!fn) throw new Error("未定义函数 " + name);
      const scope = new Map();
      (fn.params || []).forEach((p, i) =>
        scope.set(p.name, p.len === -1 ? args[i] : deepVal(args[i])));
      this.stack.push(name);                      // 调用栈（调试面板显示）
      // 环境栈底是共享全局作用域——C 里全局对所有函数可见，预览必须一致
      // （此前漏挂，函数体里读全局会无声得到 0）
      try { yield* this.execList(fn.body, [this.globalScope, scope]); }
      catch (e) { if (e instanceof ReturnSignal) return e.value; throw e; }
      finally { this.stack.pop(); }
      return 0;
    }

    // 把生成器一路跑完，忽略暂停点（用于全局初始化等不可调试的场合）
    runToEnd(gen) { let r = gen.next(); while (!r.done) r = gen.next(); return r.value; }

    // 把生成器泵到完成；命中断点/单步时挂起并返回 false
    pump(gen, ai) {
      for (;;) {
        const r = gen.next();
        if (r.done) return true;
        const { node, env } = r.value;
        if (this.onStep) this.onStep(node);           // 执行高亮（原有行为）
        if (this.stepMode || this.breakpoints.has(node)) {
          this.paused = true;
          this.stepMode = false;
          this.pending = { ai: ai, gen: gen };
          if (this.onPause) this.onPause(node, env, this.stack.slice());
          return false;
        }
      }
    }

    // ---- 调试控制（供编辑器 UI 调用）----
    dbgToggleBreakpoint(node) {
      if (this.breakpoints.has(node)) this.breakpoints.delete(node);
      else this.breakpoints.add(node);
      return this.breakpoints.has(node);
    }
    dbgResume() {                                     // 继续运行到下一个断点
      if (!this.paused) return;
      this.paused = false;
      this.frameLoop();
    }
    dbgStep() {                                       // 单步：执行下一条语句就停
      if (!this.paused) return;
      this.paused = false;
      this.stepMode = true;
      this.frameLoop();
    }
    dbgPause() { this.stepMode = true; }               // 请求在下一条语句处停下

    // 暂停现场的变量快照（由内向外合并作用域，内层遮蔽外层）
    dbgVars(env) {
      const out = [];
      const seen = new Set();
      for (let i = env.length - 1; i >= 0; i--) {
        for (const [k, v] of env[i]) {
          if (seen.has(k)) continue;
          seen.add(k);
          out.push({ name: k, value: v, scope: i === 0 ? "全局" : "局部" });
        }
      }
      return out;
    }

    // 舞台坐标（中心原点、y 向上）→ 画布坐标
    s2c(x, y) { return [x + this.world.W / 2, this.world.H / 2 - y]; }

    drawConsole() {
      const ctx = this.ctx; ctx.fillStyle = "#333"; ctx.font = "16px monospace";
      this.world.console.slice(0, 12).forEach((line, i) => ctx.fillText(line, 16, 28 + i * 22));
    }
  }

  // ---- 运行时内建（对应 runtime/prelude） ----
  SinPreview.prototype.BUILTINS = {
    stage_init(a) { this.world.W = a[0]; this.world.H = a[1];
      this.canvas.width = a[0]; this.canvas.height = a[1]; },
    stage_running() { return this.world.running; },
    frame_begin() {
      // 每帧只清一次（多精灵共享同一帧）；广播交换由 frameLoop 统一处理
      if (!this.world.frameCleared) {
        this.ctx.fillStyle = "#f5f5f7"; this.ctx.fillRect(0, 0, this.world.W, this.world.H);
        if (this.penCanvas) this.ctx.drawImage(this.penCanvas, 0, 0); // 贴回画笔持久层
        this.world.frameCleared = true;
      }
    },
    frame_end() {},
    stage_close() { this.world.running = false; },
    sprite_new(a) { const s = { kind: "rect", x: a[0], y: a[1], size: a[2], bubble: "" }; this.world.sprites.push(s); return this.world.sprites.length - 1; },
    sprite_load(a) { const s = { kind: "image", x: 0, y: 0, size: 0, bubble: "", tex: this.resolveTex(a[0]), path: a[0] }; this.world.sprites.push(s); return this.world.sprites.length - 1; },
    sprite_move_to(a) { const s = this.world.sprites[a[0]]; if (s) { s.x = a[1]; s.y = a[2]; } },
    sprite_x(a) { const s = this.world.sprites[a[0]]; return s ? s.x : 0; },
    sprite_y(a) { const s = this.world.sprites[a[0]]; return s ? s.y : 0; },
    // 两精灵是否碰撞（AABB 重叠；与 runtime rt_touching 同语义）
    sprite_touching(a) {
      const sa = this.world.sprites[a[0]], sb = this.world.sprites[a[1]];
      if (!sa || !sb || sa.hidden || sb.hidden) return false;
      const half = (s) => {
        const k = s.scale || 1;
        if (s.kind === "image" && s.tex) {
          const w = (s.tex.naturalWidth || s.tex.width || 48) * k;
          const h = (s.tex.naturalHeight || s.tex.height || 48) * k;
          return [w / 2, h / 2];
        }
        const z = (s.size || 48) * k / 2;
        return [z, z];
      };
      const [ahw, ahh] = half(sa), [bhw, bhh] = half(sb);
      return Math.abs(sa.x - sb.x) < ahw + bhw && Math.abs(sa.y - sb.y) < ahh + bhh;
    },
    sprite_draw(a) {
      const s = this.world.sprites[a[0]]; if (!s || s.hidden) return;
      const ctx = this.ctx, [cx, cy] = this.s2c(s.x, s.y), k = s.scale || 1;
      let z = s.size * k;
      if (s.kind === "image") {
        // 造型纹理：画的就是 sprite_load 的真实图像（与成品同源）
        const t = s.tex;
        const ready = t && (t.tagName === "CANVAS" ? (t.width > 0) : (t.complete && t.naturalWidth > 0));
        if (ready) {
          const tw = (t.naturalWidth || t.width) * k, th = (t.naturalHeight || t.height) * k;
          const hd = s.heading || 0;
          if (hd) {
            // 贴图跟随朝向旋转（与 runtime 一致）：heading 是舞台系逆时针角，
            // 画布 rotate 是屏幕系顺时针角——取负号
            ctx.save(); ctx.translate(cx, cy); ctx.rotate(-hd * Math.PI / 180);
            ctx.drawImage(t, -tw / 2, -th / 2, tw, th);
            ctx.restore();
          } else {
            ctx.drawImage(t, cx - tw / 2, cy - th / 2, tw, th);
          }
          if (s.bubble) { ctx.fillStyle = "#000"; ctx.font = "16px sans-serif"; ctx.fillText(s.bubble, cx + tw / 2, cy - th / 2 - 6); }
          return;
        }
        // 造型尚未加载完：临时用占位球，避免空白
        z = z || 48;
        ctx.fillStyle = "#ffd21a"; ctx.beginPath(); ctx.arc(cx, cy, z / 2, 0, 2 * Math.PI); ctx.fill();
        ctx.lineWidth = 2; ctx.strokeStyle = "#e64646"; ctx.stroke();
      } else {
        // 程序化方块：与运行时 RT_SPR_RECT（MAROON 暗红 + 黑边）一致
        ctx.fillStyle = "#be2233"; ctx.fillRect(cx - z / 2, cy - z / 2, z, z);
        ctx.lineWidth = 2; ctx.strokeStyle = "#000"; ctx.strokeRect(cx - z / 2, cy - z / 2, z, z);
      }
      if (s.bubble) { ctx.fillStyle = "#000"; ctx.font = "16px sans-serif"; ctx.fillText(s.bubble, cx + z / 2, cy - z / 2 - 6); }
    },
    key_down(a) { return this.world.keys.has(a[0]); },
    // ---- 动态列表（全局 T[*]）：与生成的 C 的 List_T 助手同语义 ----
    push(a) { a[0].push(deepVal(a[1])); },
    pop(a) { if (!a[0].length) throw new Error("对空列表 pop"); return a[0].pop(); },
    insert(a) {
      const l = a[0], i = a[1];
      if (i < 0 || i > l.length) throw new Error("insert 下标 " + i + " 越界（长度 " + l.length + "）");
      l.splice(i, 0, deepVal(a[2]));
    },
    remove_at(a) {
      const l = a[0], i = a[1];
      if (i < 0 || i >= l.length) throw new Error("remove_at 下标 " + i + " 越界（长度 " + l.length + "）");
      l.splice(i, 1);
    },
    clear(a) { a[0].length = 0; },
    // ---- 字符串库：按码点（与生成的 C 的 UTF-8 实现同语义）----
    str_len(a) { return [...String(a[0])].length; },
    str_at(a) { const cp = [...String(a[0])], i = a[1]; return i >= 0 && i < cp.length ? cp[i] : ""; },
    str_sub(a) {
      const cp = [...String(a[0])];
      const st = Math.max(0, a[1]), n = Math.max(0, a[2]);
      return cp.slice(st, st + n).join("");
    },
    str_find(a) {
      const s = String(a[0]), bi = s.indexOf(String(a[1]));
      return bi < 0 ? -1 : [...s.slice(0, bi)].length;
    },
    str_contains(a) { return String(a[0]).includes(String(a[1])); },
    str_to_int(a) { const n = parseInt(String(a[0]), 10); return isNaN(n) ? 0 : n; },
    str_to_float(a) { const n = parseFloat(String(a[0])); return isNaN(n) ? 0 : n; },
    key_pressed_space() { return this.pressedNow.has(32); },
    key_pressed_left()  { return this.pressedNow.has(263); },
    key_pressed_right() { return this.pressedNow.has(262); },
    key_pressed_up()    { return this.pressedNow.has(265); },
    key_pressed_down()  { return this.pressedNow.has(264); },
    mouse_clicked() { return this.clickNow; },
    sprite_show(a) { const s = this.world.sprites[a[0]]; if (s) s.hidden = false; },
    sprite_hide(a) { const s = this.world.sprites[a[0]]; if (s) s.hidden = true; },
    // 碰到舞台边缘就反弹：镜像朝向并夹回舞台内（与 rt_bounce 同语义）
    sprite_bounce(a) {
      const s = this.world.sprites[a[0]]; if (!s) return;
      const k = s.scale || 1, hw = (s.size || 48) * k / 2, hh = hw;
      const xmax = this.world.W / 2 - hw, ymax = this.world.H / 2 - hh;
      if (s.x > xmax)  { s.x = xmax;  s.heading = 180 - (s.heading || 0); }
      if (s.x < -xmax) { s.x = -xmax; s.heading = 180 - (s.heading || 0); }
      if (s.y > ymax)  { s.y = ymax;  s.heading = -(s.heading || 0); }
      if (s.y < -ymax) { s.y = -ymax; s.heading = -(s.heading || 0); }
    },
    sprite_touching_mouse(a) {
      const s = this.world.sprites[a[0]]; if (!s || s.hidden) return false;
      const k = s.scale || 1, hw = (s.size || 48) * k / 2, hh = hw;
      const m = this.mouse;
      return m.x >= s.x - hw && m.x <= s.x + hw && m.y >= s.y - hh && m.y <= s.y + hh;
    },
    timer() { return (performance.now() - this.timerBase) / 1000; },
    timer_reset() { this.timerBase = performance.now(); },
    key_left() { return 263; }, key_right() { return 262; }, key_up() { return 265; }, key_down_arrow() { return 264; }, key_space() { return 32; },
    mouse_x() { return this.mouse.x; }, mouse_y() { return this.mouse.y; }, mouse_down() { return this.mouse.down; },
    // 运动（精灵）
    sprite_move(a) { const s = this.world.sprites[a[0]]; if (!s) return; const rad = (s.heading || 0) * Math.PI / 180; s.x += Math.cos(rad) * a[1]; s.y += Math.sin(rad) * a[1]; },
    sprite_turn(a) { const s = this.world.sprites[a[0]]; if (s) s.heading = (s.heading || 0) + a[1]; },
    sprite_point(a) { const s = this.world.sprites[a[0]]; if (s) s.heading = a[1]; },
    sprite_scale(a) { const s = this.world.sprites[a[0]]; if (s) s.scale = a[1]; },
    // 平台 / 工具
    random_int(a) { let lo = a[0], hi = a[1]; if (lo > hi) { const t = lo; lo = hi; hi = t; } return Math.floor(Math.random() * (hi - lo + 1)) + lo; },
    screen_width() { return this.world.W; }, screen_height() { return this.world.H; },
    frame_index() { return this.world.frame; },
    // 画笔（持久层）
    pen_clear() { if (this.penCtx) this.penCtx.clearRect(0, 0, this.world.W, this.world.H); },
    pen_color(a) { this.penColor = "rgb(" + (a[0] | 0) + "," + (a[1] | 0) + "," + (a[2] | 0) + ")"; },
    pen_size(a) { this.penSize = Math.max(1, a[0]); },
    pen_line(a) { const p = this.penCtx; if (!p) return; const [x1, y1] = this.s2c(a[0], a[1]), [x2, y2] = this.s2c(a[2], a[3]); p.strokeStyle = this.penColor; p.lineWidth = this.penSize; p.lineCap = "round"; p.beginPath(); p.moveTo(x1, y1); p.lineTo(x2, y2); p.stroke(); },
    pen_dot(a) { const p = this.penCtx; if (!p) return; const [x, y] = this.s2c(a[0], a[1]); p.fillStyle = this.penColor; p.beginPath(); p.arc(x, y, this.penSize, 0, 2 * Math.PI); p.fill(); },
    say(a) { const s = this.world.sprites[a[0]]; if (s) s.bubble = a[1]; },
    draw_text(a) { const [cx, cy] = this.s2c(a[1], a[2]); this.ctx.fillStyle = "#222"; this.ctx.font = a[3] + "px monospace"; this.ctx.fillText(a[0], cx, cy); },
    draw_number(a) { const [cx, cy] = this.s2c(a[1], a[2]); this.ctx.fillStyle = "#222"; this.ctx.font = a[3] + "px monospace"; this.ctx.fillText(String(a[0]), cx, cy); },
    broadcast(a) { this.world.nextBroadcasts.add(a[0]); },
    received(a) { return this.world.broadcasts.has(a[0]); },
    sound_load() { return ++this.world.soundCount; },
    play_sound() { this.beep(660, 100); },
    play_tone(a) { this.beep(a[0], a[1]); },
    to_float(a) { return a[0]; },
    to_int(a) { return Math.trunc(a[0]); },
    // 内建 str(x)：标量转字符串（与生成的 C 语义一致；'+' 拼接在 binop 里天然可用；
    // 浮点走 %g 六位有效数字，与 sin_str_from_float 对齐）
    str(a) { const v = a[0]; return typeof v === "boolean" ? (v ? "true" : "false") : fmtNum(v); },
    // 内建 len(x)：数组/切片长度（切片在 JS 里就是同一个数组对象，天然共享）
    len(a) { const v = a[0]; return (v && v.length !== undefined) ? v.length : 0; },
    // libm 数学函数（对应 extern fn sqrt/sin/... 直接绑定 libm，程序需 -lm）
    sqrt(a) { return Math.sqrt(a[0]); },
    sin(a) { return Math.sin(a[0]); },
    cos(a) { return Math.cos(a[0]); },
    tan(a) { return Math.tan(a[0]); },
    floor(a) { return Math.floor(a[0]); },
    ceil(a) { return Math.ceil(a[0]); },
    fabs(a) { return Math.abs(a[0]); },
    fmin(a) { return Math.min(a[0], a[1]); },
    fmax(a) { return Math.max(a[0], a[1]); },
    pow(a) { return Math.pow(a[0], a[1]); },
    // libm 常用补齐（用户 extern fn 声明即可用；成品链接 -lm 的同名函数）
    round(a) { const v = a[0]; return v < 0 ? -Math.round(-v) : Math.round(v); },  // C 的 round：半数远离零
    trunc(a) { return Math.trunc(a[0]); },
    atan2(a) { return Math.atan2(a[0], a[1]); },
    asin(a) { return Math.asin(a[0]); },
    acos(a) { return Math.acos(a[0]); },
    atan(a) { return Math.atan(a[0]); },
    log(a) { return Math.log(a[0]); },
    log10(a) { return Math.log10(a[0]); },
    exp(a) { return Math.exp(a[0]); },
    fmod(a) { return a[0] % a[1]; },
    hypot(a) { return Math.hypot(a[0], a[1]); },
    print(a) { const t = fmtNum(a[0]); this.world.console.push(t); if (this.onPrint) this.onPrint(t); },
  };

  root.SinPreview = SinPreview;
})(typeof window !== "undefined" ? window : this);
