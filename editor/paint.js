// paint.js — 精灵造型编辑画板
//
// 一个基于 <canvas> 的位图绘画工具：画笔 / 橡皮 / 颜色 / 笔刷大小 /
// 清空 / 导出 PNG，并维护一组「造型」（多张图）。造型可作为精灵纹理使用。
(function (root) {
  "use strict";

  const W = 480, H = 360;
  const PALETTE = ["#000000", "#ff4d4d", "#ff8c1a", "#ffd21a", "#59c059",
                   "#4c97ff", "#9966ff", "#cf63cf", "#ffffff"];

  class CostumeEditor {
    constructor(opts) {
      this.canvas = opts.canvas;
      this.canvas.width = W; this.canvas.height = H;
      this.ctx = this.canvas.getContext("2d", { willReadFrequently: true });
      this.listEl = opts.listEl;
      this.swatchesEl = opts.swatchesEl;
      this.toolButtons = opts.toolButtons;   // { pencil, eraser }
      this.sizeInput = opts.sizeInput;

      this.tool = "pencil";
      this.color = "#4c97ff";
      this.size = 8;
      this.drawing = false;
      this.last = null;

      this.costumes = [];   // [{name, data(ImageData|null)}]
      this.current = -1;

      this._buildSwatches();
      this._bindCanvas();
      // 默认一张空白造型
      this.addCostume("造型1");
    }

    _buildSwatches() {
      if (!this.swatchesEl) return;
      this.swatchesEl.innerHTML = "";
      PALETTE.forEach((c) => {
        const sw = document.createElement("div");
        sw.className = "swatch" + (c === this.color ? " sel" : "");
        sw.style.background = c;
        sw.title = c;
        sw.addEventListener("click", () => this.setColor(c));
        sw._color = c;
        this.swatchesEl.appendChild(sw);
      });
    }

    setColor(c) {
      this.color = c;
      this.tool = "pencil";
      this._syncTools();
      if (this.swatchesEl)
        [...this.swatchesEl.children].forEach((s) =>
          s.classList.toggle("sel", s._color === c));
    }

    setTool(t) { this.tool = t; this._syncTools(); }
    setSize(n) { this.size = Math.max(1, n | 0); }

    _syncTools() {
      if (!this.toolButtons) return;
      Object.entries(this.toolButtons).forEach(([name, btn]) =>
        btn.classList.toggle("active", name === this.tool));
    }

    _pos(ev) {
      const r = this.canvas.getBoundingClientRect();
      return {
        x: (ev.clientX - r.left) * (W / r.width),
        y: (ev.clientY - r.top) * (H / r.height),
      };
    }

    _bindCanvas() {
      const down = (ev) => { this.drawing = true; this.last = this._pos(ev);
        this._dot(this.last); ev.preventDefault(); };
      const move = (ev) => { if (!this.drawing) return; const p = this._pos(ev);
        this._stroke(this.last, p); this.last = p; ev.preventDefault(); };
      const up = () => { if (this.drawing) { this.drawing = false; this._commit(); } };

      this.canvas.addEventListener("pointerdown", down);
      this.canvas.addEventListener("pointermove", move);
      window.addEventListener("pointerup", up);
    }

    _apply(ctx) {
      if (this.tool === "eraser") {
        ctx.globalCompositeOperation = "destination-out";
        ctx.strokeStyle = ctx.fillStyle = "rgba(0,0,0,1)";
      } else {
        ctx.globalCompositeOperation = "source-over";
        ctx.strokeStyle = ctx.fillStyle = this.color;
      }
      ctx.lineCap = ctx.lineJoin = "round";
      ctx.lineWidth = this.size;
    }

    _dot(p) {
      const ctx = this.ctx; this._apply(ctx);
      ctx.beginPath(); ctx.arc(p.x, p.y, this.size / 2, 0, Math.PI * 2); ctx.fill();
    }

    _stroke(a, b) {
      const ctx = this.ctx; this._apply(ctx);
      ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke();
    }

    // ---- 造型管理 ----
    // 切换到某个精灵的造型集（每个精灵各自独立的一组造型）
    setStore(arr) {
      this._commit();                 // 先把当前画布存回旧造型集
      this.costumes = arr;
      if (this.costumes.length === 0)
        this.costumes.push({ name: "造型1", data: null });
      this.current = -1;
      this.select(0);
    }

    addCostume(name) {
      this._commit();
      this.costumes.push({ name: name || ("造型" + (this.costumes.length + 1)), data: null });
      this.select(this.costumes.length - 1);
      this._renderList();
    }

    select(i) {
      if (i < 0 || i >= this.costumes.length) return;
      this._commit();
      this.current = i;
      this.ctx.clearRect(0, 0, W, H);
      const d = this.costumes[i].data;
      if (d) this.ctx.putImageData(d, 0, 0);
      this._renderList();
    }

    _commit() {
      if (this.current < 0) return;
      this.costumes[this.current].data = this.ctx.getImageData(0, 0, W, H);
      this._renderList();
    }

    clear() {
      this.ctx.clearRect(0, 0, W, H);
      this._commit();
    }

    toDataURL() { return this.canvas.toDataURL("image/png"); }

    // 把当前画布写回当前造型（保存项目前调用，确保未提交的笔画也存下）
    flush() { this._commit(); }

    _renderList() {
      if (!this.listEl) return;
      this.listEl.innerHTML = "<h2>造型</h2>";
      this.costumes.forEach((c, i) => {
        const t = document.createElement("div");
        t.className = "costume-thumb" + (i === this.current ? " sel" : "");
        // 缩略图：把当前画布或已存数据画到小图
        if (i === this.current) t.style.backgroundImage = "url(" + this.toDataURL() + ")";
        else if (c.data) {
          const off = document.createElement("canvas");
          off.width = W; off.height = H;
          off.getContext("2d").putImageData(c.data, 0, 0);
          t.style.backgroundImage = "url(" + off.toDataURL() + ")";
        }
        t.title = c.name;
        t.addEventListener("click", () => this.select(i));
        this.listEl.appendChild(t);
      });
      const add = document.createElement("button");
      add.className = "pal-block"; add.style.background = "#59c059";
      add.textContent = "+ 新造型";
      add.addEventListener("click", () => this.addCostume());
      this.listEl.appendChild(add);
    }
  }

  root.CostumeEditor = CostumeEditor;
})(typeof window !== "undefined" ? window : this);
