// verify_events.js — 事件驱动 + 示例模板库的浏览器端验证
//   node verify_events.js <index.html 绝对路径>
// 断言：
//   1) 每个模板载入后零诊断（模板源码永远是能跑的——坏模板比没模板更劝退）
//   2) 弹弹球模板：事件帽以中文短语渲染（当⚑被点击/每一帧/当按下空格键），
//      预览无 main 也跑起来（合成驱动），画布出现动画像素
//   3) 调色板 8 顶事件帽齐全；点「当按下 ↑」→ 文本出现 fn on_key_up()
//   4) 同一事件不重复创建（再点一次 on_key_up 仍只有一个）
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const net = require("net");
const path = require("path");
function freePort(){return new Promise(r=>{const s=net.createServer();s.listen(0,"127.0.0.1",()=>{const p=s.address().port;s.close(()=>r(p));});});}
(async () => {
  const htmlPath = process.argv[2];
  if (!htmlPath) { console.error("缺少 index.html 路径"); process.exit(2); }
  const port = await freePort();
  const srv = spawn("python3",["-m","http.server",String(port),"--bind","127.0.0.1","--directory",path.dirname(htmlPath)],{stdio:"ignore"});
  const b = await chromium.launch();
  const p = await b.newPage({viewport:{width:1440,height:900}});
  const errs=[]; p.on("pageerror",e=>errs.push(e.message));
  const res={};
  try{
    for(let i=0;i<10;i++){try{await p.goto(`http://127.0.0.1:${port}/index.html`,{waitUntil:"networkidle"});break;}catch(e){if(i===9)throw e;await new Promise(r=>setTimeout(r,500));}}
    await p.waitForFunction(()=>window.__sincReady===true,{timeout:15000});
    await p.waitForTimeout(600);

    // 1) 全部模板逐个载入：必须零诊断
    res.templates = {};
    const ids = await p.evaluate(()=>window.SIN_TEMPLATES.map(t=>t.id));
    for (const id of ids) {
      await p.evaluate((i)=>window._sinTemplates.load(i), id);
      await p.waitForTimeout(900);
      res.templates[id] = await p.evaluate(()=>document.getElementById("text-status").textContent);
    }
    res.templatesOk = Object.values(res.templates).every((s)=>s.includes("已同步"));

    // 2) 弹弹球：事件帽 + 无 main 预览
    await p.evaluate(()=>window._sinTemplates.load("bounce"));
    await p.waitForTimeout(1500);
    const hats = await p.evaluate(()=>[...document.querySelectorAll("#canvas .event-hat > .hdr > .label")].map(e=>e.textContent));
    res.hats = hats;
    res.hatsOk = ["当 ⚑ 被点击","每一帧","当按下 空格键"].every((h)=>hats.includes(h));
    res.pixels = await p.evaluate(()=>{
      const c=document.getElementById("preview-canvas"), d=c.getContext("2d").getImageData(0,0,c.width,c.height).data;
      let n=0; for(let i=0;i<d.length;i+=4) if(!(d[i]>230&&d[i+1]>230&&d[i+2]>230)) n++;
      return n;
    });

    // 3) 调色板事件帽齐全 + 点击创建
    res.palKinds = await p.evaluate(()=>[...document.querySelectorAll('.pal-wys[data-kind^="addEvent"]')].map(e=>e.dataset.kind));
    const clickEv = () => p.evaluate(()=>{
      const wys=document.querySelector('.pal-wys[data-kind="addEvent:on_key_up"]');
      const r=wys.getBoundingClientRect();
      wys.dispatchEvent(new PointerEvent("pointerdown",{bubbles:true,button:0,clientX:r.left+5,clientY:r.top+5}));
      window.dispatchEvent(new PointerEvent("pointerup",{bubbles:true}));
    });
    await clickEv(); await p.waitForTimeout(600);
    const t1 = await p.inputValue("#text-out");
    res.onKeyUpAdded = /fn on_key_up\(\)/.test(t1);
    // 4) 再点一次：不重复
    await clickEv(); await p.waitForTimeout(600);
    const t2 = await p.inputValue("#text-out");
    res.noDup = (t2.match(/fn on_key_up\(\)/g)||[]).length===1;
  }catch(e){res.error=String(e).slice(0,200);}
  res.errors=errs;
  res.ok = res.templatesOk && res.hatsOk && res.pixels>500 &&
           res.palKinds && res.palKinds.length===8 && res.onKeyUpAdded && res.noDup &&
           errs.length===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
