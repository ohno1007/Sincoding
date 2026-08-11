// verify_shared_blocks.js — 验证结构体 / 全局变量可从积木创建与编辑
//   node verify_shared_blocks.js <index.html 绝对路径> [截图]
// 此前二者既不能从调色板创建，也不在画布上显示 —— 只能敲文本。
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const net = require("net");
const path = require("path");
function freePort(){return new Promise(r=>{const s=net.createServer();s.listen(0,"127.0.0.1",()=>{const p=s.address().port;s.close(()=>r(p));});});}
const clickPal = (kind) => `
  (() => { const w=document.querySelector('.pal-wys[data-kind="${kind}"]');
    if(!w) throw new Error("调色板缺少 ${kind}");
    const r=w.getBoundingClientRect();
    w.dispatchEvent(new PointerEvent("pointerdown",{bubbles:true,button:0,clientX:r.left+5,clientY:r.top+5}));
    window.dispatchEvent(new PointerEvent("pointerup",{bubbles:true})); })()`;
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
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      ta.value='fn main() -> int { return 0 }'; ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(1000);

    // ① 从调色板新建结构体 → 文本应出现 struct
    await p.evaluate(clickPal("addStruct"));
    await p.waitForTimeout(800);
    res.structAdded = /^struct Point1 \{/m.test(await p.inputValue("#text-out"));
    // 结构体块应显示在画布上
    res.structOnCanvas = (await p.$$(".block.struct-blk")).length === 1;

    // ② 给结构体加字段并改成数组类型
    await p.evaluate(()=>{ document.querySelector(".block.struct-blk .add-param").click(); });
    await p.waitForTimeout(600);
    await p.evaluate(()=>{
      const fs=[...document.querySelectorAll(".block.struct-blk .field")];
      const f=fs[fs.length-1];                 // 最后一个字段的类型
      f.focus(); f.textContent="float[4]";
      f.dispatchEvent(new Event("input",{bubbles:true}));
    });
    await p.waitForTimeout(700);
    res.arrayField = /f3: float\[4\]/.test(await p.inputValue("#text-out"));

    // ③ 从调色板新建全局变量
    await p.evaluate(clickPal("addGlobal"));
    await p.waitForTimeout(800);
    const txt = await p.inputValue("#text-out");
    res.globalAdded = /^let g1: int = 0/m.test(txt);
    res.globalOnCanvas = (await p.$$(".block.global-blk")).length === 1;
    res.head = txt.split("\n").slice(0, 6).join(" | ");
    if (shot) await p.screenshot({path: shot});
  }catch(e){res.error=String(e).slice(0,180);}
  res.errors=errs;
  res.ok = res.structAdded && res.structOnCanvas && res.arrayField &&
           res.globalAdded && res.globalOnCanvas && errs.length===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
