// verify_publish.js —— 端到端验证「发布」：浏览器模态框 → 本地构建服务 → 产物
//   node verify_publish.js [platform]   （默认 linux，编译最快）
// 启动 tools/ide_server.py，打开编辑器，发布模态框只勾选目标平台并点「开始编译」，
// 断言结果行变为成功、给出可下载链接，且产物确实落在 editor/dist 下。
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const net = require("net");
const path = require("path");
const fs = require("fs");

function freePort() {
  return new Promise((res) => {
    const s = net.createServer();
    s.listen(0, "127.0.0.1", () => { const p = s.address().port; s.close(() => res(p)); });
  });
}

(async () => {
  const plat = process.argv[2] || "linux";
  const root = path.resolve(__dirname, "..");
  const port = await freePort();
  const srv = spawn("python3", [path.join(root, "tools/ide_server.py"), String(port)],
    { stdio: "ignore", env: process.env });
  await new Promise((r) => setTimeout(r, 1200));

  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1240, height: 800 } });
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  let result = {};
  try {
    await page.goto(`http://127.0.0.1:${port}/index.html`, { waitUntil: "networkidle" });
    await page.waitForSelector("#canvas .script");
    await page.click("#btn-publish");
    await page.waitForSelector("#publish-modal:not([hidden])");
    // 只勾选目标平台
    await page.evaluate((target) => {
      document.querySelectorAll(".pub-plat").forEach((c) => { c.checked = (c.value === target); });
    }, plat);
    await page.click("#publish-go");
    // 等待结果行出现成功标记（编译可能耗时，给足时间）
    await page.waitForFunction(() => {
      const rows = document.querySelectorAll("#pub-results .row");
      return rows.length > 0 && [...rows].every((r) => !r.classList.contains("") || r.querySelector(".badge").textContent !== "…");
    }, { timeout: 240000 }).catch(() => {});
    await page.waitForTimeout(500);
    const ui = await page.evaluate(() => {
      const rows = [...document.querySelectorAll("#pub-results .row")];
      const ok = rows.some((r) => r.classList.contains("ok"));
      const href = (rows.find((r) => r.querySelector("a")) || {}).querySelector
        ? rows.map((r) => r.querySelector("a")).filter(Boolean).map((a) => a.getAttribute("href"))[0] : null;
      const status = document.getElementById("pub-status").textContent;
      return { ok, href, status };
    });
    // 产物落盘校验（href 形如 dist/<name>/<plat>/...，相对 editor/）
    let artifactExists = false;
    if (ui.href) artifactExists = fs.existsSync(path.join(root, "editor", ui.href));
    result = { platform: plat, uiOk: ui.ok, href: ui.href, status: ui.status, artifactExists, errors };
    result.ok = ui.ok && !!ui.href && artifactExists && errors.length === 0;
  } finally {
    await browser.close();
    srv.kill();
  }
  console.log(JSON.stringify(result));
  process.exit(result.ok ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
