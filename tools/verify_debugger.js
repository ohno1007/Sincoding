// verify_debugger.js — 验证积木级调试器：断点 / 暂停 / 变量面板 / 调用栈 / 单步
//   node verify_debugger.js <index.html 绝对路径> [截图路径]
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const net = require("net");
const path = require("path");
function freePort(){return new Promise(r=>{const s=net.createServer();s.listen(0,"127.0.0.1",()=>{const p=s.address().port;s.close(()=>r(p));});});}
(async () => {
  const htmlPath = process.argv[2], shot = process.argv[3];
  if (!htmlPath) { console.error("缺少 index.html 路径"); process.exit(2); }
  const port = await freePort();
  const srv = spawn("python3",["-m","http.server",String(port),"--bind","127.0.0.1","--directory",path.dirname(htmlPath)],{stdio:"ignore"});
  await new Promise(r=>setTimeout(r,800));
  const b = await chromium.launch();
  const p = await b.newPage({viewport:{width:1240,height:800}});
  const errs=[]; p.on("pageerror",e=>errs.push(e.message));
  const res={};
  try{
    await p.goto(`http://127.0.0.1:${port}/index.html`,{waitUntil:"networkidle"});
    await p.waitForFunction(()=>window.__sincReady===true,{timeout:15000});
    // 一个带主循环的程序：每帧给 tick 加一，便于观察变量
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      ta.value=['extern fn stage_init(w: int, h: int)','extern fn stage_running() -> bool',
        'extern fn frame_begin()','extern fn frame_end()','let tick = 0',
        'fn bump(n: int) -> int { return n + 1 }',
        'fn main() -> int {','    stage_init(480, 360)','    while stage_running() {',
        '        tick = bump(tick)','        frame_begin()','        frame_end()','    }','    return 0','}'].join('\n');
      ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(1500);   // 等积木渲染 + 预览开跑
    res.running = await p.$eval("#pv-status", e=>e.textContent);

    // Alt+点击第一条循环体内的语句 → 设断点
    res.bpSet = await p.evaluate(()=>{
      // 取循环体内的赋值语句块（"赋 tick = bump(tick)"）——注意函数定义块不是语句块
      const blks=[...document.querySelectorAll("#canvas .block")];
      const t=blks.find(b=>b.textContent.startsWith("赋"));
      if(!t) return false;
      const r=t.getBoundingClientRect();
      t.dispatchEvent(new PointerEvent("pointerdown",{bubbles:true,button:0,altKey:true,
        clientX:r.left+5,clientY:r.top+5}));
      return t.classList.contains("bp");
    });
    await p.waitForTimeout(1200);   // 等下一帧命中断点

    res.paused = await p.$eval("#dbg-panel", e=>!e.hidden).catch(()=>false);
    res.where = await p.$eval("#dbg-where", e=>e.textContent).catch(()=>"");
    res.vars = await p.$$eval("#dbg-vars .dbg-row .n", e=>e.map(x=>x.textContent));
    res.stack = await p.$$eval("#dbg-stack .dbg-frame", e=>e.map(x=>x.textContent));
    res.pausedAt = await p.$$eval(".block.paused-at", e=>e.length);
    if (shot) await p.screenshot({path: shot});

    // 单步：应仍处于暂停态（停在下一条）
    await p.click("#dbg-step");
    await p.waitForTimeout(600);
    res.stillPausedAfterStep = await p.$eval("#dbg-panel", e=>!e.hidden).catch(()=>false);
  }catch(e){res.error=String(e).slice(0,180);}
  res.errors=errs;
  res.ok = res.bpSet && res.paused && res.vars.includes("tick") &&
           res.stack.length>=1 && res.pausedAt===1 && res.stillPausedAfterStep && errs.length===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
