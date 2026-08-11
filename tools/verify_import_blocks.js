// verify_import_blocks.js — 验证 import 在积木侧不丢失、且可从调色板可视化添加
//   node verify_import_blocks.js <index.html 绝对路径>
// 断言：
//   1) 文本里的 import，在编辑积木后仍保留（此前会被静默删掉）
//   2) 调色板「模块 / 库」能添加 import，添加后库积木自动出现
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
  await new Promise(r=>setTimeout(r,800));
  const b = await chromium.launch();
  const p = await b.newPage({viewport:{width:1240,height:800}});
  const errs=[]; p.on("pageerror",e=>errs.push(e.message));
  const res={};
  try{
    await p.goto(`http://127.0.0.1:${port}/index.html`,{waitUntil:"networkidle"});
    await p.waitForFunction(()=>window.__sincReady===true,{timeout:15000});

    // ① import 不因积木编辑而丢失
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      ta.value='import "std/arrayx"\nfn main() -> int {\n    let a: int[3] = [3,1,2]\n    sort(a)\n    return sum(a)\n}';
      ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(1200);
    await p.evaluate(() => {
      const f=[...document.querySelectorAll("#canvas .field.lit")].find(x=>/^\d+$/.test(x.textContent.trim()));
      if(f){ f.focus(); f.textContent="9"; f.dispatchEvent(new Event("input",{bubbles:true})); }
    });
    await p.waitForTimeout(800);
    res.importKept = /^import "std\/arrayx"/m.test(await p.inputValue("#text-out"));

    // ② 从调色板可视化添加 import（清空文本后，用「导入库」加 std/mathx）
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      ta.value='fn main() -> int { return 0 }';
      ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(900);
    await p.evaluate(()=>{ window.prompt = () => "std/mathx"; });
    await p.evaluate(()=>{
      const w=[...document.querySelectorAll('.pal-wys[data-kind="addImport"]')][0];
      if(!w) throw new Error("调色板没有「导入库」积木");
      const r=w.getBoundingClientRect();
      w.dispatchEvent(new PointerEvent("pointerdown",{bubbles:true,button:0,clientX:r.left+5,clientY:r.top+5}));
      window.dispatchEvent(new PointerEvent("pointerup",{bubbles:true}));
    });
    await p.waitForTimeout(1200);
    const txt = await p.inputValue("#text-out");
    res.importAdded = /^import "std\/mathx"/m.test(txt);
    res.libCats = (await p.$$eval(".cat-btn .cat-nm", e=>e.map(x=>x.textContent))).filter(c=>c.includes("📦"));
  }catch(e){res.error=String(e).slice(0,180);}
  res.errors=errs;
  res.ok = res.importKept && res.importAdded && (res.libCats||[]).length>=1 && errs.length===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
