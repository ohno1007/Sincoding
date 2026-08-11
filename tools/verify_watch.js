// verify_watch.js — 舞台变量监视器的浏览器端验证
//   node verify_watch.js <index.html 绝对路径>
// 断言：
//   1) 带全局变量的程序跑起来后，舞台叠加层出现 名字=取值 行（Scratch 式监视器）
//   2) 值实时更新（帧循环里自增的全局，两次采样值不同）
//   3) 列表全局显示为 [a, b, …]
//   4) 点「变量」按钮可隐藏，再点恢复
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const net = require("net");
const path = require("path");
function freePort(){return new Promise(r=>{const s=net.createServer();s.listen(0,"127.0.0.1",()=>{const p=s.address().port;s.close(()=>r(p));});});}

const SRC = `let score = 0
let names: int[*]

fn on_start() {
    push(names, 7)
    push(names, 8)
}

fn on_frame() {
    score = score + 1
}
`;

(async () => {
  const htmlPath = process.argv[2];
  if (!htmlPath) { console.error("缺少 index.html 路径"); process.exit(2); }
  const port = await freePort();
  const srv = spawn("python3",["-m","http.server",String(port),"--bind","127.0.0.1","--directory",path.dirname(htmlPath)],{stdio:"ignore"});
  const b = await chromium.launch();
  const p = await b.newPage({viewport:{width:1440,height:900}});
  const errs=[]; p.on("pageerror",e=>errs.push(e.message));
  const res={};
  const watchRows = () => p.evaluate(()=>[...document.querySelectorAll("#pv-watchers .watch-row")]
    .map(r=>r.querySelector(".watch-nm").textContent+"="+r.querySelector(".watch-val").textContent));
  try{
    for(let i=0;i<10;i++){try{await p.goto(`http://127.0.0.1:${port}/index.html`,{waitUntil:"networkidle"});break;}catch(e){if(i===9)throw e;await new Promise(r=>setTimeout(r,500));}}
    await p.waitForFunction(()=>window.__sincReady===true,{timeout:15000});
    await p.waitForTimeout(600);
    await p.evaluate((v)=>{const ta=document.getElementById("text-out");
      ta.value=v; ta.dispatchEvent(new Event("input",{bubbles:true}));}, SRC);
    await p.waitForTimeout(1400);

    // 1) 出现监视行
    const r1 = await watchRows();
    res.rows1 = r1;
    res.hasScore = r1.some((x)=>/^score=\d+$/.test(x));
    res.hasList = r1.some((x)=>x.startsWith("names=[7, 8"));

    // 2) 帧循环自增 → 值在变
    await p.waitForTimeout(800);
    const r2 = await watchRows();
    res.rows2 = r2;
    const num = (rows)=>{const m=rows.find((x)=>x.startsWith("score=")); return m?parseInt(m.slice(6),10):-1;};
    res.updates = num(r2) > num(r1);

    // 3/4) 开关
    await p.click("#pv-watch");
    await p.waitForTimeout(400);
    res.hidden = await p.evaluate(()=>document.getElementById("pv-watchers").hidden===true &&
                                       window._sinWatch.isOn()===false);
    await p.click("#pv-watch");
    await p.waitForTimeout(400);
    res.back = (await watchRows()).length >= 2;
  }catch(e){res.error=String(e).slice(0,300);}
  res.errors=errs;
  res.ok = res.hasScore && res.hasList && res.updates && res.hidden && res.back && errs.length===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
