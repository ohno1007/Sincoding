// verify_complete.js — 验证代码补全走的是**引擎**（sin_complete），而非前端猜名字
//   node verify_complete.js <index.html 绝对路径>
// 断言：
//   1) 运行时 API（sprite_*）在候选里且带真实签名 —— 前端已不再自带声明表
//   2) 结构体成员补全（f. → 字段 + 字段类型）
//   3) 作用域内变量带真实类型（含数组长度）
//   4) 用了 sprite_new 不再被报「未定义的函数」（std/stage 隐式可见）
//   5) 导出的 .sin 自带运行时声明，且包含 sprite_touching（旧前端表漏了它）
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const net = require("net");
const path = require("path");
function freePort(){return new Promise(r=>{const s=net.createServer();s.listen(0,"127.0.0.1",()=>{const p=s.address().port;s.close(()=>r(p));});});}

const PROG = `struct Faller { x: float, y: float, kind: int }

fn tick(data: int[4]) -> int {
    let h = sprite_new(0.0, 0.0, 40.0)
    let f: Faller
    sprite_move_to(h, 1.0, 2.0)
    return 0
}

fn main() -> int {
    let d: int[4]
    return tick(d)
}
`;

// 在光标处插入 text 并请求补全，返回候选（text|kind|detail）
function acAt(anchor, text) {
  const ta = document.getElementById("text-out");
  const off = ta.value.indexOf(anchor);
  ta.value = ta.value.slice(0, off) + text + "\n" + ta.value.slice(off);
  ta.focus();
  ta.setSelectionRange(off + text.length, off + text.length);
  window._sinAC.show();
  return window._sinAC.state.items.map((i) => i.text + "|" + i.kind + "|" + i.detail);
}

(async () => {
  const htmlPath = process.argv[2];
  if (!htmlPath) { console.error("缺少 index.html 路径"); process.exit(2); }
  const port = await freePort();
  const srv = spawn("python3", ["-m", "http.server", String(port), "--bind", "127.0.0.1",
                                "--directory", path.dirname(htmlPath)], { stdio: "ignore" });
  await new Promise((r) => setTimeout(r, 800));
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1240, height: 800 } });
  const errs = []; p.on("pageerror", (e) => errs.push(e.message));
  const res = {};
  try {
    await p.goto(`http://127.0.0.1:${port}/index.html`, { waitUntil: "networkidle" });
    await p.waitForFunction(() => window.__sincReady === true, { timeout: 15000 });
    await p.evaluate((s) => {
      const ta = document.getElementById("text-out");
      ta.value = s; ta.dispatchEvent(new Event("input", { bubbles: true }));
    }, PROG);
    await p.waitForTimeout(600);

    // 4) 运行时函数不再被报未定义
    res.status = await p.evaluate(() => document.getElementById("text-status").textContent);

    await p.addScriptTag({ content: "window.__acAt = " + acAt.toString() + ";" });

    // 1) 运行时 API 带签名
    res.runtime = await p.evaluate(() => window.__acAt("    return 0", "    sprite_mo"));
    // 2) 成员补全
    res.member = await p.evaluate(() => window.__acAt("    return 0", "    f."));
    // 3) 变量带真实类型
    res.vars = await p.evaluate(() => window.__acAt("    return 0", "    da"));
    // 5) 导出自带运行时声明
    res.exported = await p.evaluate(() => {
      const s = window._sinExport ? window._sinExport() : "";
      return { hasTouching: /^extern fn sprite_touching/m.test(s), lines: s.split("\n").length };
    });
  } catch (e) { res.error = String(e).slice(0, 200); }
  res.errors = errs;
  const has = (arr, re) => Array.isArray(arr) && arr.some((x) => re.test(x));
  res.ok = res.status === "已同步 ✓" &&
           has(res.runtime, /^sprite_move_to\|fn\|\(s: int, x: float, y: float\)/) &&
           has(res.member, /^kind\|field\|int · Faller/) &&
           has(res.vars, /^data\|var\|int\[4\] · 参数/) &&
           !!(res.exported && res.exported.hasTouching) &&
           errs.length === 0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok ? 0 : 1);
})();
