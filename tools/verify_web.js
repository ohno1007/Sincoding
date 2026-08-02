// verify_web.js — 在 Chromium 中加载 wasm 成品并截图验证渲染
//   node verify_web.js <url> <out_png>
const { chromium } = require("playwright");

(async () => {
  const url = process.argv[2];
  const shot = process.argv[3];
  const browser = await chromium.launch({ args: ["--use-gl=swiftshader", "--enable-webgl", "--ignore-gpu-blocklist"] });
  const page = await browser.newPage({ viewport: { width: 900, height: 760 } });
  const errors = [];
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
  page.on("pageerror", (e) => errors.push("PE: " + e.message));

  await page.goto(url, { waitUntil: "load" });
  await page.waitForFunction(
    () => { const c = document.getElementById("canvas"); return c && c.width > 0 && c.height > 0; },
    { timeout: 20000 });
  await page.waitForTimeout(4000); // 让 raylib 渲染若干帧
  await page.locator("#canvas").screenshot({ path: shot });

  await browser.close();
  console.log(JSON.stringify({ errors }));
})().catch((e) => { console.error(e); process.exit(1); });
