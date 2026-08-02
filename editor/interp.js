// interp.js — 积木模型解释器 + 实时预览
//
// 直接在浏览器里「跑」积木模型（不经编译）：解释表达式/语句/函数，并把
// 运行时内建（精灵/移动/文字/按键/广播/声音）画到预览画布上。游戏主循环
// while stage_running() {...} 被特殊处理为 requestAnimationFrame 逐帧执行，
// 从而不阻塞页面。编辑积木/文本即可重跑（类 Vue 实时预览）。
(function (root) {
  "use strict";

  const KEYMAP = { ArrowLeft: 263, ArrowRight: 262, ArrowUp: 265, ArrowDown: 264, " ": 32 };

  class ReturnSignal { constructor(v) { this.value = v; } }

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
      this.onPrint = null;
      canvas.tabIndex = 0;
      canvas.addEventListener("keydown", (e) => {
        const c = KEYMAP[e.key]; if (c !== undefined) { this.keys.add(c); e.preventDefault(); }
      });
      canvas.addEventListener("keyup", (e) => {
        const c = KEYMAP[e.key]; if (c !== undefined) this.keys.delete(c);
      });
      // 鼠标：记录舞台坐标（中心原点、y 向上）与按下状态
      this.mouse = { x: 0, y: 0, down: false };
      canvas.addEventListener("mousemove", (e) => {
        const r = canvas.getBoundingClientRect();
        const cx = (e.clientX - r.left) * (canvas.width / r.width);
        const cy = (e.clientY - r.top) * (canvas.height / r.height);
        this.mouse.x = cx - canvas.width / 2;
        this.mouse.y = canvas.height / 2 - cy;
      });
      canvas.addEventListener("mousedown", () => { this.mouse.down = true; });
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
      // 共享全局作用域：所有精灵 actor 的 env 栈底都是同一个 Map，
      // 因此一个精灵对全局数组/结构体/变量的修改对其它精灵立即可见。
      this.structDefs = {};
      (shared && shared.structs || []).forEach((s) => { this.structDefs[s.name] = s; });
      this.globalScope = new Map();
      this.fns = {};
      (shared && shared.globals || []).forEach((g) => {
        this.globalScope.set(g.name, g.value !== undefined ? this.eval(g.value, [this.globalScope]) : this.defaultVal(g));
      });
      this.actors = [];
      let hasMain = false;
      for (const program of (programs || [])) {
        const fns = {};
        (program || []).forEach((f) => { if (f.block === "fn") fns[f.name] = f; });
        const main = fns["main"];
        if (!main) continue;
        hasMain = true;
        const env = [this.globalScope, new Map()];
        const body = main.body || [];
        const loopIdx = body.findIndex((s) => s.block === "while" && s.cond &&
          s.cond.block === "call" && s.cond.callee === "stage_running");
        this.fns = fns;
        try {
          if (loopIdx < 0) {
            this.execList(body, env);            // 无主循环：跑到结束
          } else {
            for (let i = 0; i < loopIdx; i++) this.execStmt(body[i], env);  // 初始化
            this.actors.push({ fns, env, loop: body[loopIdx], post: body.slice(loopIdx + 1) });
          }
        } catch (e) {
          if (!(e instanceof ReturnSignal)) this.onStatus("运行出错: " + e.message, "warn");
        }
      }
      if (!hasMain) { this.onStatus("没有 main()，无法预览", "warn"); return; }
      if (this.actors.length === 0) { this.drawConsole(); this.onStatus("运行完成 ✓", "ok"); return; }
      this.onStatus(this.actors.length > 1 ?
        (this.actors.length + " 个精灵并行运行 ▶") : "运行中 ▶（点画面用方向键/空格）", "ok");
      this.frameLoop();
    }

    frameLoop() {
      const wd = this.world;
      if (!wd || !wd.running) {
        (this.actors || []).forEach((a) => { this.fns = a.fns; try { for (const s of a.post) this.execStmt(s, a.env); } catch (e) {} });
        if (wd) this.onStatus("已结束", "ok");
        return;
      }
      wd.frameCleared = false;
      wd.broadcasts = wd.nextBroadcasts; wd.nextBroadcasts = new Set();
      for (const a of this.actors) {
        this.fns = a.fns;
        try { this.execList(a.loop.body, a.env); }
        catch (e) {
          if (e instanceof ReturnSignal) a.done = true;
          else { this.onStatus("运行出错: " + e.message, "warn"); return; }
        }
      }
      this.actors = this.actors.filter((a) => !a.done);
      if (this.actors.length === 0) wd.running = false;
      wd.frame++;
      this.raf = requestAnimationFrame(() => this.frameLoop());
    }

    // ---- 语句 ----
    execList(list, env) {
      env.push(new Map());
      try { for (const s of list) this.execStmt(s, env); }
      finally { env.pop(); }
    }

    execStmt(node, env) {
      const w = this.world;
      if (this.onStep) this.onStep(node);   // 执行到该积木 → 高亮回调
      switch (node.block) {
        case "let": {
          let v = node.value !== undefined ? this.eval(node.value, env) : this.defaultVal(node);
          env[env.length - 1].set(node.name, v);
          break;
        }
        case "assign": {
          if (node.field) { const o = this.lookup(env, node.name); if (o) o[node.field] = this.eval(node.value, env); }
          else if (node.index) { const a = this.lookup(env, node.name); a[this.eval(node.index, env)] = this.eval(node.value, env); }
          else this.setVar(env, node.name, this.eval(node.value, env));
          break;
        }
        case "if":
          if (this.eval(node.cond, env)) this.execList(node.then, env);
          else if (node.else) this.execList(node.else, env);
          break;
        case "while": {
          let g = 0;
          while (this.eval(node.cond, env)) {
            this.execList(node.body, env);
            if (++g > 2000000) throw new Error("循环次数过多");
          }
          break;
        }
        case "for": {
          const a = this.eval(node.start, env), b = this.eval(node.end, env);
          env.push(new Map());
          try { for (let i = a; i < b; i++) { env[env.length - 1].set(node.var, i); this.execList(node.body, env); } }
          finally { env.pop(); }
          break;
        }
        case "return":
          throw new ReturnSignal(node.value !== undefined ? this.eval(node.value, env) : 0);
        case "expr":
          this.eval(node.expr, env);
          break;
      }
    }

    defaultVal(node) {
      if (node.len > 0) return new Array(node.len).fill(node.type === "float" ? 0 : (node.type === "bool" ? false : (node.type === "string" ? "" : 0)));
      if (node.type === "bool") return false;
      if (node.type === "string") return "";
      if (node.type === "float" || node.type === "int") return 0;
      return {}; // 结构体零初始化
    }

    // ---- 表达式 ----
    eval(node, env) {
      switch (node.block) {
        case "int": case "float": case "bool": return node.value;
        case "string": return node.value;
        case "var": return this.lookup(env, node.name);
        case "unary": { const v = this.eval(node.operand, env); return node.op === "-" ? -v : !v; }
        case "binary": return this.binop(node.op, this.eval(node.lhs, env), this.eval(node.rhs, env));
        case "call": return this.callFn(node.callee, node.args.map((a) => this.eval(a, env)));
        case "index": return this.eval(node.arr, env)[this.eval(node.idx, env)];
        case "array": return node.elems.map((e) => this.eval(e, env));
        case "field": { const o = this.eval(node.obj, env); const v = o ? o[node.name] : undefined; return v === undefined ? 0 : v; }
        case "structlit": { const o = {}; node.fields.forEach((f) => { o[f.name] = this.eval(f.value, env); }); return o; }
      }
      return 0;
    }

    binop(op, l, r) {
      switch (op) {
        case "+": return l + r; case "-": return l - r; case "*": return l * r;
        case "/": return (Number.isInteger(l) && Number.isInteger(r)) ? Math.trunc(l / r) : l / r;
        case "%": return l % r;
        case "==": return l === r; case "!=": return l !== r;
        case "<": return l < r; case "<=": return l <= r; case ">": return l > r; case ">=": return l >= r;
        case "&&": return l && r; case "||": return l || r;
      }
      return 0;
    }

    lookup(env, name) { for (let i = env.length - 1; i >= 0; i--) if (env[i].has(name)) return env[i].get(name); return 0; }
    setVar(env, name, v) { for (let i = env.length - 1; i >= 0; i--) if (env[i].has(name)) { env[i].set(name, v); return; } env[env.length - 1].set(name, v); }

    callFn(name, args) {
      const b = this.BUILTINS[name];
      if (b) return b.call(this, args);
      const fn = this.fns[name];
      if (!fn) throw new Error("未定义函数 " + name);
      const scope = new Map();
      (fn.params || []).forEach((p, i) => scope.set(p.name, args[i]));
      try { this.execList(fn.body, [scope]); }
      catch (e) { if (e instanceof ReturnSignal) return e.value; throw e; }
      return 0;
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
      if (!sa || !sb) return false;
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
      const s = this.world.sprites[a[0]]; if (!s) return;
      const ctx = this.ctx, [cx, cy] = this.s2c(s.x, s.y), k = s.scale || 1;
      let z = s.size * k;
      if (s.kind === "image") {
        // 造型纹理：画的就是 sprite_load 的真实图像（与成品同源）
        const t = s.tex;
        const ready = t && (t.tagName === "CANVAS" ? (t.width > 0) : (t.complete && t.naturalWidth > 0));
        if (ready) {
          const tw = (t.naturalWidth || t.width) * k, th = (t.naturalHeight || t.height) * k;
          ctx.drawImage(t, cx - tw / 2, cy - th / 2, tw, th);
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
    // 内建 str(x)：标量转字符串（与生成的 C 语义一致；'+' 拼接在 binop 里天然可用）
    str(a) { const v = a[0]; return typeof v === "boolean" ? (v ? "true" : "false") : String(v); },
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
    print(a) { const t = String(a[0]); this.world.console.push(t); if (this.onPrint) this.onPrint(t); },
  };

  root.SinPreview = SinPreview;
})(typeof window !== "undefined" ? window : this);
