// verify_rename.js — 用 Chromium 验证 IDE 的 mini-LSP 前端接入
//   node verify_rename.js <index.html 绝对路径>
// 断言：F2 重命名（改文本 + 作用域正确）、光标处 hover 显示类型、无 JS 报错。
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const net = require("net");
const path = require("path");

function freePort() {
  return new Promise((res) => {
    const s = net.createServer();
    s.listen(0, "127.0.0.1", () => { const p = s.address().port; s.close(() => res(p)); });
  });
}

(async () => {
  const htmlPath = process.argv[2];
  if (!htmlPath) { console.error("缺少 index.html 路径"); process.exit(2); }
  const dir = path.dirname(htmlPath);
  const port = await freePort();
  const srv = spawn("python3", ["-m", "http.server", String(port), "--bind", "127.0.0.1", "--directory", dir], { stdio: "ignore" });
  await new Promise((r) => setTimeout(r, 800));

  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1240, height: 800 } });
  const errors = [];
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
  page.on("pageerror", (e) => errors.push("PAGEERR: " + e.message));

  const PROG = "fn f() -> int {\n    let x = 1\n    return x\n}\nfn g() -> int {\n    let x = 2\n    return x\n}\nfn main() -> int { return f() + g() }";
  let result = {};
  try {
    await page.goto(`http://127.0.0.1:${port}/index.html`, { waitUntil: "networkidle" });
    await page.waitForFunction(() => window.__sincReady === true, { timeout: 15000 });

    // 装载已知程序到文本框，光标放到 f 里的 x（第2行的 "x"）
    await page.evaluate((src) => {
      const ta = document.getElementById("text-out");
      ta.value = src;
      ta.dispatchEvent(new Event("input", { bubbles: true }));
    }, PROG);
    await page.waitForTimeout(400);

    // —— hover：把光标放到 f 的 x 定义处，应显示「变量 x : int」——
    const hoverText = await page.evaluate(() => {
      const ta = document.getElementById("text-out");
      const off = ta.value.indexOf("let x") + 4; // 'x' 的偏移
      ta.setSelectionRange(off, off); ta.focus();
      ta.dispatchEvent(new KeyboardEvent("keyup", { key: "ArrowRight", bubbles: true }));
      return document.getElementById("text-status").textContent;
    });
    result.hoverOk = /变量\s+x\s*:\s*int/.test(hoverText);

    // —— F2 重命名：f 的 x → y，g 的 x 应保留 ——
    await page.evaluate(() => { window.prompt = () => "y"; });
    await page.evaluate(() => {
      const ta = document.getElementById("text-out");
      const off = ta.value.indexOf("let x") + 4;
      ta.setSelectionRange(off, off); ta.focus();
      ta.dispatchEvent(new KeyboardEvent("keydown", { key: "F2", bubbles: true, cancelable: true }));
    });
    await page.waitForTimeout(400);
    const after = await page.inputValue("#text-out");
    // f 里应出现 let y / return y，g 里仍是 let x / return x
    result.renameOk = /let y/.test(after) && /let x/.test(after) &&
                      after.indexOf("let y") < after.indexOf("let x"); // y 在 f（前），x 在 g（后）
    result.after = after.replace(/\n/g, "\\n");
  } catch (e) {
    result.error = String(e).slice(0, 200);
  }
  result.errors = errors;
  result.ok = result.hoverOk && result.renameOk && errors.length === 0;
  console.log(JSON.stringify(result));
  await browser.close();
  srv.kill();
  process.exit(result.ok ? 0 : 1);
})();
