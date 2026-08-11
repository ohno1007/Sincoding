// verify_lib_palette.js — 验证「导入即得积木」：import 库后调色板自动出现该库分类
//   node verify_lib_palette.js <index.html 绝对路径> [截图路径]
// 断言：库分类出现、积木由签名生成（void→语句块 / 有返回值→reporter）、无 JS 报错。
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const net = require("net");
const path = require("path");
function freePort(){return new Promise(r=>{const s=net.createServer();s.listen(0,"127.0.0.1",()=>{const p=s.address().port;s.close(()=>r(p));});});}
(async () => {
  const htmlPath = process.argv[2];
  const shot = process.argv[3];
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
    res.libCatsBefore = (await p.$$eval(".cat-btn .cat-nm", e=>e.map(x=>x.textContent))).filter(c=>c.includes("std/")).length;
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      ta.value='import "std/arrayx"\nfn main() -> int {\n    let a: int[3] = [3,1,2]\n    sort(a)\n    return sum(a)\n}';
      ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(900);
    res.libCats = (await p.$$eval(".cat-btn .cat-nm", e=>e.map(x=>x.textContent))).filter(c=>c.includes("std/"));
    res.libBlocks = await p.$$eval('.pal-wys[data-kind^="lib:"]', e=>e.map(x=>x.dataset.kind));
    // 就地函数(void)应是语句块，取值函数应在 reporter 分类里
    res.sortIsStmt = await p.$$eval('.pal-wys[data-kind="lib:sort"]', e=>e.length>0 && !e[0].classList.contains("pal-reporter"));
    res.sumIsReporter = await p.$$eval('.pal-wys[data-kind="lib:sum"]', e=>e.length>0 && e[0].classList.contains("pal-reporter"));
    if (shot) {
      await p.evaluate(() => {
        const sec = [...document.querySelectorAll(".cat-sec")].find(s => s.id.startsWith("cat-lib_"));
        if (sec) sec.parentElement.scrollTop = sec.offsetTop - 6;   // 直接定位，不用 smooth
      });
      await p.waitForTimeout(400);
      await p.screenshot({path: shot});
    }
  }catch(e){res.error=String(e).slice(0,150);}
  res.errors=errs;
  res.ok = res.libCats && res.libCats.length>=2 && res.libBlocks && res.libBlocks.length>=8 &&
           res.sortIsStmt && res.sumIsReporter && errs.length===0 && res.libCatsBefore===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
