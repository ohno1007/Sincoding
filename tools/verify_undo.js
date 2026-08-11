// verify_undo.js — 编辑器信任底线三件套的浏览器端验证
//   node verify_undo.js <index.html 绝对路径>
// 断言：
//   1) 注释保真：带注释的程序 → 改积木（调色板加语句）→ 文本仍保留全部注释
//   2) 撤销：Ctrl+Z 撤掉刚加的积木（文本回到加块前）；重做恢复
//   3) 复制：Ctrl+拖拽把「设」积木复制一份（模型里多一条 let）
//   4) 函数复制：右键函数头 → 画布出现 fib_2
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const net = require("net");
const path = require("path");
function freePort(){return new Promise(r=>{const s=net.createServer();s.listen(0,"127.0.0.1",()=>{const p=s.address().port;s.close(()=>r(p));});});}

const PROG = `// 文件头说明
import "std/mathx"  // 数学库

// 主函数
fn main() -> int {
    let a = 1  // 行尾注释
    print(a)
    return 0
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
  try{
    for(let i=0;i<10;i++){try{await p.goto(`http://127.0.0.1:${port}/index.html`,{waitUntil:"networkidle"});break;}catch(e){if(i===9)throw e;await new Promise(r=>setTimeout(r,500));}}
    await p.waitForFunction(()=>window.__sincReady===true,{timeout:15000});
    await p.waitForTimeout(600);

    // 装入带注释的程序
    await p.evaluate((s)=>{const ta=document.getElementById("text-out");
      ta.value=s; ta.dispatchEvent(new Event("input",{bubbles:true}));}, PROG);
    await p.waitForTimeout(700);

    // 1) 改积木：从调色板点一个「设」加进画布 → 文本重写后注释必须还在
    await p.evaluate(()=>{
      const wys=document.querySelector('.pal-wys[data-kind="let"]');
      const r=wys.getBoundingClientRect();
      wys.dispatchEvent(new PointerEvent("pointerdown",{bubbles:true,button:0,clientX:r.left+5,clientY:r.top+5}));
      window.dispatchEvent(new PointerEvent("pointerup",{bubbles:true,clientX:r.left+5,clientY:r.top+5}));
    });
    await p.waitForTimeout(600);
    const t1 = await p.inputValue("#text-out");
    res.commentsKept = ["// 文件头说明","// 数学库","// 主函数","// 行尾注释"].every((c)=>t1.includes(c));
    res.blockAdded = (t1.match(/let x: int = 0/g)||[]).length===1;

    // 2) 撤销 / 重做
    await p.evaluate(()=>{ document.getElementById("canvas").focus?.(); });
    await p.keyboard.press("Control+z");
    await p.waitForTimeout(500);
    const t2 = await p.inputValue("#text-out");
    res.undone = !t2.includes("let x: int = 0") && t2.includes("// 文件头说明");
    await p.keyboard.press("Control+y");
    await p.waitForTimeout(500);
    const t3 = await p.inputValue("#text-out");
    res.redone = t3.includes("let x: int = 0");

    // 3) Ctrl+拖拽复制「设」积木：落到函数体 stack 中部
    const dup = await p.evaluate(()=>new Promise((resolve)=>{
      const blks=[...document.querySelectorAll("#canvas .block")];
      const t=blks.find(b=>b.textContent.startsWith("设"));
      const r=t.getBoundingClientRect();
      t.dispatchEvent(new PointerEvent("pointerdown",{bubbles:true,button:0,ctrlKey:true,
        clientX:r.left+8,clientY:r.top+8}));
      // 拖离 5px 阈值再放回原位置（落点=同一个 stack）
      window.dispatchEvent(new PointerEvent("pointermove",{clientX:r.left+8,clientY:r.top+40}));
      window.dispatchEvent(new PointerEvent("pointermove",{clientX:r.left+8,clientY:r.top+20}));
      setTimeout(()=>{
        window.dispatchEvent(new PointerEvent("pointerup",{clientX:r.left+8,clientY:r.top+20}));
        setTimeout(()=>resolve(true),400);
      },50);
    }));
    await p.waitForTimeout(500);
    const t4 = await p.inputValue("#text-out");
    // 画布上第一个「设」积木可能是 let a 也可能是 let x：断言 let 总数 +1 即复制成功
    const lets = (s)=>(s.match(/^\s*let /gm)||[]).length;
    res.ctrlDragDup = dup && lets(t4) === lets(t3) + 1;

    // 4) 右键函数头 → 复制整个函数
    await p.evaluate(()=>{
      const hdr=document.querySelector("#canvas .block.hat.fn > .hdr");
      const r=hdr.getBoundingClientRect();
      hdr.dispatchEvent(new MouseEvent("contextmenu",{bubbles:true,cancelable:true,clientX:r.left+10,clientY:r.top+10}));
    });
    await p.waitForTimeout(600);
    const t5 = await p.inputValue("#text-out");
    res.fnDup = /fn main_2\(/.test(t5);
  }catch(e){res.error=String(e).slice(0,200);}
  res.errors=errs;
  res.ok = res.commentsKept && res.blockAdded && res.undone && res.redone &&
           res.ctrlDragDup && res.fnDup && errs.length===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
