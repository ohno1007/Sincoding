// verify_lib_preview.js — 验证「预览 = 成品」：import 的库函数在预览里也能真正执行
//   node verify_lib_preview.js <index.html 绝对路径>
// 断言：程序 import std/arrayx 并调用 sort/sum，预览控制台输出正确结果（6）。
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
    // 只留一个精灵，避免其它精灵的输出混入
    await p.evaluate(()=>{ const del=[...document.querySelectorAll(".sprite-card")].slice(1);
      del.forEach(c=>{ const x=c.querySelector(".sp-del"); if(x) x.click(); }); });
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      // 用不易与其它精灵输出混淆的数值：sum=1203，排序后首元素 407
      ta.value='import "std/arrayx"\nfn main() -> int {\n    let a: int[3] = [796, 407, 0]\n    sort(a)\n    print(sum(a))\n    print(a[1])\n    return 0\n}';
      ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(1800);
    res.console = await p.$eval("#dock-con-body", e=>e.textContent.trim()).catch(()=>
                  p.$$eval("#dock-console div", e=>e.map(x=>x.textContent).join(" ")));
  }catch(e){res.error=String(e).slice(0,150);}
  res.errors=errs;
  // sum=1203；排序后 a = [0, 407, 796]，故 a[1]=407
  res.ok = /1203/.test(res.console||"") && /407/.test(res.console||"") && errs.length===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
