// verify_fn_signature.js — 验证函数签名可从积木编辑（参数增删/类型/返回类型）
//   node verify_fn_signature.js <index.html 绝对路径> [截图]
// 此前函数参数在积木上是**只读文本**，数组/切片/泛型参数只能敲文本才能用。
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
    await p.evaluate(()=>{const ta=document.getElementById("text-out");
      ta.value='fn total(xs: int[3]) -> int {\n    return xs[0]\n}\nfn main() -> int { return 0 }';
      ta.dispatchEvent(new Event("input",{bubbles:true}));});
    await p.waitForTimeout(1200);

    // ① 已有参数应显示完整类型（含长度），此前会退化成 int
    res.showsLen = await p.$$eval("#canvas .block.hat .param", els =>
      els.some(e => /int\[3\]/.test(e.textContent)));

    // ② 点「+参数」添加一个参数
    await p.evaluate(()=>{
      const btn=[...document.querySelectorAll("#canvas .block.hat .add-param")][0];
      if(!btn) throw new Error("函数头没有「+参数」按钮");
      btn.click();
    });
    await p.waitForTimeout(700);
    res.added = /a2: int/.test(await p.inputValue("#text-out"));

    // ③ 把新参数类型改成切片 int[]
    await p.evaluate(()=>{
      const fields=[...document.querySelectorAll("#canvas .block.hat .param .field")];
      const f=fields[fields.length-1];               // 最后一个参数的类型字段
      f.focus(); f.textContent="int[]";
      f.dispatchEvent(new Event("input",{bubbles:true}));
    });
    await p.waitForTimeout(700);
    res.sliceParam = /a2: int\[\]/.test(await p.inputValue("#text-out"));

    // ④ 改返回类型为 float
    await p.evaluate(()=>{
      const ks=[...document.querySelectorAll("#canvas .block.hat .field.kw")];
      const f=ks[ks.length-1];
      f.focus(); f.textContent="float";
      f.dispatchEvent(new Event("input",{bubbles:true}));
    });
    await p.waitForTimeout(700);
    res.retChanged = /-> float/.test(await p.inputValue("#text-out"));
    res.text = (await p.inputValue("#text-out")).split("\n")[0];
    if (shot) await p.screenshot({path: shot});
  }catch(e){res.error=String(e).slice(0,180);}
  res.errors=errs;
  res.ok = res.showsLen && res.added && res.sliceParam && res.retChanged && errs.length===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
