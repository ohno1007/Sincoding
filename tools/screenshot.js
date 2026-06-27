// screenshot.js — 用预装 Chromium 给一个本地 HTML 页面截图
//   node screenshot.js <html_path> <out_png>
// 通过 NODE_PATH 找到全局安装的 playwright。
const { chromium } = require('playwright');

(async () => {
  const htmlPath = process.argv[2];
  const outPath = process.argv[3];
  if (!htmlPath || !outPath) {
    console.error('用法: node screenshot.js <html_path> <out_png>');
    process.exit(2);
  }
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 900, height: 700 } });
  await page.goto('file://' + htmlPath, { waitUntil: 'networkidle' });
  const h = await page.evaluate(() => document.body.scrollHeight);
  await page.setViewportSize({ width: 900, height: Math.min(2400, h + 24) });
  await page.screenshot({ path: outPath });
  await browser.close();
  console.log('已截图:', outPath);
})().catch(e => { console.error(e); process.exit(1); });
