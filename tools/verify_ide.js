// verify_ide.js — 用 Chromium 验证积木 IDE 的关键交互
//   node verify_ide.js <index.html 绝对路径> [shots_dir]
// 经本地 HTTP 服务加载（wasm 编译器需要 fetch，不支持 file://）。
// 断言：积木渲染、字段写回、拖拽重排、调色板、文本→积木反向同步（wasm）、
//       多精灵、造型画板、舞台纹理、无 JS 报错。
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
  const shots = process.argv[3];
  if (!htmlPath) { console.error("缺少 index.html 路径"); process.exit(2); }
  const dir = path.dirname(htmlPath);
  const port = await freePort();
  const srv = spawn("python3", ["-m", "http.server", String(port), "--bind", "127.0.0.1", "--directory", dir],
    { stdio: "ignore" });
  await new Promise((r) => setTimeout(r, 800));

  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1240, height: 800 } });
  const errors = [];
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
  page.on("pageerror", (e) => errors.push("PAGEERR: " + e.message));

  const val = () => page.inputValue("#text-out");
  const dragTo = async (fx, fy, tx, ty) => {
    await page.mouse.move(fx, fy); await page.mouse.down();
    await page.mouse.move((fx + tx) / 2, (fy + ty) / 2, { steps: 8 });
    await page.mouse.move(tx, ty, { steps: 10 }); await page.waitForTimeout(60);
    await page.mouse.up(); await page.waitForTimeout(120);
  };
  let result = {};
  try {
    await page.goto(`http://127.0.0.1:${port}/index.html`, { waitUntil: "networkidle" });
    await page.waitForSelector("#canvas .script");
    if (shots) await page.screenshot({ path: shots + "/ide_blocks.png" });

    // 写回：改一个数字字面量 → 文本视图应反映
    const before = await val();
    await page.evaluate(() => {
      const f = [...document.querySelectorAll("#canvas .field.lit")].find((x) => /^\d+$/.test(x.textContent.trim()));
      f.focus(); f.textContent = "77321";
      f.dispatchEvent(new Event("input", { bubbles: true }));
    });
    const after = await val();
    const writeback = after.includes("77321") && after !== before;

    // 积木拖拽重排：把 fib 体内「返回」拖到「如果」上方 → 文本顺序改变
    const boxes = await page.evaluate(() => {
      const stack = document.querySelector("#canvas .script .mouth > .stack");
      return [...stack.children].filter((c) => c.classList.contains("block"))
        .map((bl) => { const r = bl.getBoundingClientRect(); return { x: r.x, y: r.y }; });
    });
    let reorder = false;
    if (boxes.length >= 2) {
      const beforeR = await val();
      await page.mouse.move(boxes[1].x + 14, boxes[1].y + 9); await page.mouse.down();
      await page.mouse.move(boxes[1].x + 14, boxes[1].y - 30, { steps: 5 });
      await page.mouse.move(boxes[0].x + 14, boxes[0].y - 4, { steps: 6 });
      await page.mouse.up();
      await page.waitForTimeout(60);
      const afterR = await val();
      const fibBody = afterR.slice(afterR.indexOf("fn fib"));
      reorder = afterR !== beforeR && fibBody.indexOf("return") < fibBody.indexOf("if (");
    }

    // 调色板（仿 Scratch：分类导航 + 所见即所得积木）：加入「数组」与「for」积木
    await page.click('#palette .pal-wys[data-kind="let_arr"]');
    await page.click('#palette .pal-wys[data-kind="for"]');
    await page.waitForTimeout(40);
    const palText = await val();
    const palette = palText.includes("int[3]") && palText.includes("for ");
    // 分类导航：点一个分类按钮应高亮
    await page.click('#palette .cat-rail .cat-btn:nth-child(6)');
    await page.waitForTimeout(60);
    const catNav = await page.evaluate(() => document.querySelectorAll('#palette .cat-btn.active').length === 1);

    // 从调色板把积木拖进控制块「嘴巴」（嵌套进 if/while 内部）
    const pit = await page.$('#palette .pal-wys[data-kind="print"]');
    await pit.scrollIntoViewIfNeeded();
    const pib = await pit.boundingBox();
    const mb0 = await (await page.$('#canvas .script .mouth .stack')).boundingBox();
    const beforeDrag = await val();
    await dragTo(pib.x + pib.width / 2, pib.y + pib.height / 2, mb0.x + 28, mb0.y + 10);
    const paletteDrag = (await val()).includes("print") && (await val()) !== beforeDrag;

    // 右键删除积木
    const beforeDel = await val();
    await page.click('#canvas .script .mouth .stack .block', { button: "right" });
    await page.waitForTimeout(90);
    const blockDelete = (await val()).length < beforeDel.length;

    // 把 reporter（字符串）拖进某个表达式槽 → 嵌套表达式
    const rep = await page.$('#palette .pal-wys[data-kind="r_str"]');
    await rep.scrollIntoViewIfNeeded();
    const rb = await rep.boundingBox();
    const slot = await page.$('#canvas .script .expr-slot');
    const sb = await slot.boundingBox();
    const beforeNest = await val();
    await dragTo(rb.x + rb.width / 2, rb.y + rb.height / 2, sb.x + sb.width / 2, sb.y + sb.height / 2);
    const exprNest = (await val()).includes('"文字"') && (await val()) !== beforeNest;

    // 变量下拉：积木里的变量渲染成下拉框，自动列出作用域里的多个变量
    const varDropdown = await page.evaluate(() => {
      const sels = [...document.querySelectorAll("#canvas .var-select")];
      const opts = new Set(sels.flatMap((s) => [...s.options].map((o) => o.value)));
      return sels.length >= 2 && opts.size >= 2;
    });

    // 反向同步：编辑文本 → wasm 编译器解析 → 积木更新
    await page.waitForFunction(() => window.__sincReady === true, { timeout: 20000 });
    await page.fill("#text-out", "fn demo() -> int {\n  let z: int = 9\n  return z\n}\n");
    await page.waitForTimeout(600); // 去抖 250ms + 解析
    const reverse = await page.evaluate(() =>
      [...document.querySelectorAll("#canvas .script .field")].some((f) => f.textContent === "demo"));

    // 项目级共享状态：当前在精灵1（上面反向同步已把它改成 demo 弃用程序），
    // 在其文本里加一个全局变量；切到精灵2（游戏）后，该全局应同样出现——
    // 证明结构体/全局变量为所有精灵共享（不会动到精灵2自身的游戏程序）。
    await page.fill("#text-out", "let shared_hp: int = 99\n\nfn demo() -> int {\n  return shared_hp\n}\n");
    await page.waitForTimeout(600);

    // 多精灵 / 多页积木：切到第二个精灵（游戏）→ 文本页应切换
    const s1 = await val();
    await page.click("#sprite-list .sprite-card:nth-child(2)");
    await page.waitForTimeout(80);
    const s2 = await val();
    const spriteSwitch = s2 !== s1 && s2.includes("sprite_new");
    const sharedState = s2.includes("shared_hp") && s2.includes("99");

    // 实时预览：切到游戏精灵后，解释器应在预览画布跑出非空画面
    await page.waitForTimeout(1000);
    const previewPx = await page.evaluate(() => {
      const c = document.getElementById("preview-canvas");
      const d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data;
      let n = 0; for (let i = 0; i < d.length; i += 4) { const r = d[i], g = d[i + 1], b = d[i + 2]; if (!(r > 230 && g > 230 && b > 230)) n++; }
      return n;
    });
    const preview = previewPx > 200;
    // 造型纹理：弹球精灵用 sprite_load("coin.png")，预览应画出金币造型本身
    // （而非占位球/方块）。检测金币特有的暗金描边/十字（r~190,g~130,b~0）像素。
    await page.waitForTimeout(400);
    const costumePx = await page.evaluate(() => {
      const c = document.getElementById("preview-canvas");
      const d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data;
      let n = 0; for (let i = 0; i < d.length; i += 4) {
        const r = d[i], g = d[i + 1], b = d[i + 2];
        if (r > 150 && r < 225 && g > 95 && g < 175 && b < 45) n++;
      }
      return n;
    });
    const costume = costumePx > 15;
    // 多精灵并行：状态栏应显示「并行」
    const pvStatus = await page.evaluate(() => document.getElementById("pv-status").textContent);
    const parallel = /并行/.test(pvStatus);
    // 积木/代码页内「预览 ⇄ 控制台」切页：切到控制台 → 画布隐藏、控制台显示并有输出
    await page.click('.pv-tab[data-dock="console"]');
    await page.click("#dock-print-info");
    await page.waitForTimeout(80);
    const dockConsole = await page.evaluate(() => {
      const con = document.getElementById("dock-console"), cv = document.getElementById("preview-canvas");
      const out = document.getElementById("dock-con-out");
      return con && !con.hidden && cv && cv.hidden && out && out.querySelectorAll(".con-line").length > 0;
    });
    await page.click('.pv-tab[data-dock="preview"]'); // 切回预览，便于后续观察
    // 一键导出：补全 extern 声明，可独立编译
    const exp = await page.evaluate(() => (window._sinExport ? window._sinExport() : ""));
    const exportOk = exp.includes("extern fn stage_init") && exp.includes("fn main");
    // 语法高亮：高亮层应有关键字 span
    const highlighted = await page.evaluate(() =>
      document.querySelectorAll("#text-hl code .hl-kw").length > 0);

    // 代码补全：输入前缀 → 下拉出现含匹配项 → Enter 补全
    await page.fill("#text-out", "fn main() -> int {\n  \n  return 0\n}\n");
    await page.click("#text-out");
    await page.evaluate(() => {
      const ta = document.getElementById("text-out");
      const p = ta.value.indexOf("\n  \n") + 3; ta.setSelectionRange(p, p); ta.focus();
    });
    await page.keyboard.type("spri", { delay: 20 });
    await page.waitForTimeout(150);
    const acOpen = await page.evaluate(() => {
      const pop = document.getElementById("ac-pop");
      const items = [...document.querySelectorAll("#ac-pop .ac-item .ac-t")].map((e) => e.textContent);
      return { visible: pop && !pop.hidden, hasSprite: items.some((t) => t.startsWith("sprite")) };
    });
    await page.keyboard.press("Enter"); // 接受第一个补全
    await page.waitForTimeout(60);
    const acAccepted = await page.evaluate(() => /sprite\w+/.test(document.getElementById("text-out").value));
    const autocomplete = acOpen.visible && acOpen.hasSprite && acAccepted;

    // 语法诊断：写一段有错误的代码（未定义变量）→ 诊断面板应列出问题
    await page.fill("#text-out", "fn main() -> int {\n  return zzz_undefined\n}\n");
    await page.waitForTimeout(700);
    const diagnostics = await page.evaluate(() => {
      const list = document.getElementById("diag-list");
      return list && !list.hidden && list.querySelectorAll(".diag-row").length > 0;
    });

    // 造型画板：切 tab，画几笔，断言有像素
    await page.click('header .tabs button[data-view="costume-view"]');
    await page.waitForSelector("#paint-canvas");
    // 造型列表显示名称 + 可新增（仿 Scratch）：加一个造型，断言出现命名造型卡
    await page.click("#costume-list .costume-add");
    await page.waitForTimeout(80);
    const costumeNames = await page.evaluate(() => {
      const cards = document.querySelectorAll("#costume-list .costume-card");
      const named = [...document.querySelectorAll("#costume-list .costume-nm")].some((n) => /造型/.test(n.textContent));
      return cards.length >= 2 && named;
    });
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

    // 舞台：精灵以造型为纹理
    await page.click('header .tabs button[data-view="stage-view"]');
    await page.waitForTimeout(100);
    const stage = await page.evaluate(() => {
      const els = [...document.querySelectorAll("#stage .stage-sprite")];
      return { count: els.length, textured: els.filter((e) => e.style.backgroundImage && e.style.backgroundImage !== "none").length };
    });
    const stageOk = stage.count >= 2 && stage.textured >= 1;
    // 舞台控制台：点「打印积木信息」→ 控制台应出现输出行
    await page.click("#con-print-info");
    await page.waitForTimeout(60);
    const consolePanel = await page.evaluate(() => {
      const out = document.getElementById("console-out");
      return !!out && out.querySelectorAll(".con-line").length > 0;
    });
    if (shots) await page.screenshot({ path: shots + "/ide_stage.png" });

    // 保存/打开项目：序列化项目（含精灵/积木/造型）→ 改个名字 → 重新载入 → 生效
    const saveOpen = await page.evaluate(async () => {
      const data = window._sinSerializeProject ? window._sinSerializeProject() : null;
      if (!data || data.format !== "sincoding-project" || !data.sprites.length) return false;
      if (!("program" in data.sprites[0]) || !("costumes" in data.sprites[0])) return false;
      data.sprites[0].name = "回环精灵";
      const ok = await window._sinLoadProject(data);
      const has = [...document.querySelectorAll("#sprite-list .nm")].some((n) => n.textContent === "回环精灵");
      return ok && has;
    });

    // 发布模态框：点「发布」应弹出，含平台勾选与「开始编译」按钮
    await page.click("#btn-publish");
    await page.waitForTimeout(120);
    const publishModal = await page.evaluate(() => {
      const m = document.getElementById("publish-modal");
      const open = m && !m.hidden;
      const plats = document.querySelectorAll(".pub-plat").length;
      const go = !!document.getElementById("publish-go");
      const nameFld = !!document.getElementById("pub-name");
      return open && plats >= 4 && go && nameFld;
    });
    await page.click("#publish-close");

    result = { writeback, reorder, palette, catNav, paletteDrag, blockDelete, exprNest, varDropdown, reverse, spriteSwitch, sharedState, preview, costume, costumeNames, parallel, dockConsole, exportOk, highlighted, autocomplete, diagnostics, consolePanel, saveOpen, publishModal, stage, paintedPixels: painted, errors };
    result.ok = writeback && reorder && palette && catNav && paletteDrag && blockDelete && exprNest && varDropdown && reverse && spriteSwitch && sharedState && preview && costume && costumeNames && parallel && dockConsole &&
      exportOk && highlighted && autocomplete && diagnostics && consolePanel && saveOpen && publishModal && stageOk && painted > 100 && errors.length === 0;
  } finally {
    await browser.close();
    srv.kill();
  }

  console.log(JSON.stringify(result));
  process.exit(result.ok ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
