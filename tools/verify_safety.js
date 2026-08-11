// verify_safety.js — 数据安全批的浏览器端验证
//   node verify_safety.js <index.html 绝对路径>
// 断言：
//   1) 文本多敲一个 { → 积木数不变（保持 last-good）、状态栏提示语法有误、parseBad=true
//   2) 此时序列化项目 = last-good（语句一块不少）；修好语法后恢复同步
//   3) 恶意源码（struct Pair<T>）不再挂死引擎（曾永久冻结标签页）
//   4) 出错积木带红标（.blk-err）
//   5) 损坏 .sinproj（sprites 含 null / 未来版本号）→ 明确报错不炸页面
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
  const blocks = () => p.evaluate(()=>document.querySelectorAll("#canvas .block").length);
  const stmts = () => p.evaluate(()=>{
    let n=0; const walk=(l)=>(l||[]).forEach((s)=>{n++;walk(s.then);walk(s.else);walk(s.body);});
    window._sin.project.sprites[window._sin.project.cur].program.forEach((f)=>walk(f.body));
    return n;
  });
  try{
    for(let i=0;i<10;i++){try{await p.goto(`http://127.0.0.1:${port}/index.html`,{waitUntil:"networkidle"});break;}catch(e){if(i===9)throw e;await new Promise(r=>setTimeout(r,500));}}
    await p.waitForFunction(()=>window.__sincReady===true,{timeout:15000});
    await p.waitForTimeout(600);

    // 装一个已知程序
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      ta.value=['fn on_frame() {','    let a = 1','    let b = 2','    print(a + b)','}'].join('\n');
      ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(700);
    const s0 = await stmts();

    // 1) 多敲一个 { → 语法错误：积木必须不变
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      ta.value=ta.value.replace("fn on_frame() {","fn on_frame() { {");
      ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(700);
    res.stmtsKept = (await stmts()) === s0;
    res.parseBad = await p.evaluate(()=>window._sinParseBad());
    res.status = await p.$eval("#text-status", e=>e.textContent);

    // 2) 此时序列化 = last-good
    res.serializedKept = await p.evaluate((want)=>{
      const pj = window._sinSerializeProject();
      let n=0; const walk=(l)=>(l||[]).forEach((s)=>{n++;walk(s.then);walk(s.else);walk(s.body);});
      pj.sprites[pj.cur].program.forEach((f)=>walk(f.body));
      return n === want;
    }, s0);
    // 修好语法 → 恢复
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      ta.value=ta.value.replace("fn on_frame() { {","fn on_frame() {");
      ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(700);
    res.recovered = !(await p.evaluate(()=>window._sinParseBad()));

    // 3) 恶意源码不挂死（曾死循环冻结标签页）
    const t0 = Date.now();
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      ta.value='struct Pair<T> { a: T, b: T }\nfn main() -> int { return 0 }';
      ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(800);
    res.noHang = (Date.now() - t0) < 5000 &&
                 await p.evaluate(()=>document.getElementById("text-status").textContent.length > 0);
    res.genericDiag = await p.evaluate(()=>document.getElementById("diag-list").textContent.includes("暂不支持泛型"));

    // 4) 类型错误的积木带红标
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      ta.value=['fn on_frame() {','    let a = 1','    print(a + nope)','}'].join('\n');
      ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(700);
    res.errMarked = await p.evaluate(()=>document.querySelectorAll("#canvas .block.blk-err").length) >= 1;

    // 5) 损坏 / 未来版本的 .sinproj：明确拒绝不炸
    await p.evaluate(()=>{ window._alerts=[]; window.alert=(m)=>window._alerts.push(m); });
    res.futureVer = await p.evaluate(async()=>{
      const ok = await window._sinLoadProject({format:"sincoding-project",version:99,sprites:[{name:"x",program:[]}]});
      return ok === false && window._alerts.some((m)=>m.includes("升级"));
    });
    res.nullSprite = await p.evaluate(async()=>{
      try { await window._sinLoadProject({format:"sincoding-project",version:1,cur:0,
        sprites:[null,{name:"好精灵",program:[{block:"fn",name:"main",params:[],ret:"int",
          body:[{block:"return",value:{block:"int",value:0}}]}]}]});
        return true;   // 不炸即可（损坏条目被跳过）
      } catch (e) { return false; }
    });
  }catch(e){res.error=String(e).slice(0,200);}
  res.errors=errs;
  res.ok = res.stmtsKept && res.parseBad && /语法有误/.test(res.status) && res.serializedKept &&
           res.recovered && res.noHang && res.genericDiag && res.errMarked &&
           res.futureVer && res.nullSprite && errs.length===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
