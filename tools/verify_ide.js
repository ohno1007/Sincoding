// verify_ide.js — 用 Chromium 验证积木 IDE 的关键交互
//   node verify_ide.js <index.html 绝对路径> [shots_dir]
// 断言：积木渲染、字段编辑写回文本、造型画板可绘制、无 JS 报错。
// 全部通过则退出 0，否则非 0。
const { chromium } = require("playwright");

(async () => {
  const htmlPath = process.argv[2];
  const shots = process.argv[3];
  if (!htmlPath) { console.error("缺少 index.html 路径"); process.exit(2); }

  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1240, height: 800 } });
  const errors = [];
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
  page.on("pageerror", (e) => errors.push("PAGEERR: " + e.message));

  await page.goto("file://" + htmlPath, { waitUntil: "networkidle" });
  await page.waitForSelector("#canvas .script");
  if (shots) await page.screenshot({ path: shots + "/ide_blocks.png" });

  // 写回：改一个数字字面量 → 文本视图应反映
  const before = await page.textContent("#text-out");
  const after = await page.evaluate(() => {
    const f = [...document.querySelectorAll(".field.lit")]
      .find((x) => /^\d+$/.test(x.textContent.trim()));
    if (!f) return null;
    f.focus(); f.textContent = "77321";
    f.dispatchEvent(new Event("input", { bubbles: true }));
    return document.getElementById("text-out").textContent;
  });
  const writeback = !!after && after.includes("77321") && after !== before;

  // 造型画板：切 tab，画几笔，断言有像素
  await page.click('header .tabs button[data-view="costume-view"]');
  await page.waitForSelector("#paint-canvas");
  const box = await page.locator("#paint-canvas").boundingBox();
  await page.mouse.move(box.x + 80, box.y + 80); await page.mouse.down();
  await page.mouse.move(box.x + 220, box.y + 150, { steps: 8 });
  await page.mouse.move(box.x + 320, box.y + 90, { steps: 8 });
  await page.mouse.up();
  const painted = await page.evaluate(() => {
    const c = document.getElementById("paint-canvas");
    const d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data;
    let a = 0; for (let i = 3; i < d.length; i += 4) if (d[i] > 0) a++;
    return a;
  });
  if (shots) await page.screenshot({ path: shots + "/ide_costume.png" });

  await browser.close();

  const ok = writeback && painted > 100 && errors.length === 0;
  console.log(JSON.stringify({ writeback, paintedPixels: painted, errors }));
  process.exit(ok ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
