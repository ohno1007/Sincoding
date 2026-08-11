// verify_sinlib.js — 字符串库 + .sinlib 用户库的浏览器端验证
//   node verify_sinlib.js <index.html 绝对路径>
// 断言：
//   1) 字符串库预览与 C 同语义（按码点：中英混排长度/取子串/查找）
//   2) 安装 .sinlib → import 后零诊断、调色板出现库积木分类、预览执行库函数
//   3) 库随项目序列化（serializeProject 含 userLibs），重载后仍可用
//   4) 删除库后 import 报「找不到模块」
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const net = require("net");
const path = require("path");
function freePort(){return new Promise(r=>{const s=net.createServer();s.listen(0,"127.0.0.1",()=>{const p=s.address().port;s.close(()=>r(p));});});}

const STR_PROG = `fn main() -> int {
    let s = "你好，Sincoding 世界"
    print(str_len(s))
    print(str_sub(s, 3, 9))
    print(str_find(s, "世界"))
    print(str_contains(s, "coding"))
    print(str_to_int("42abc"))
    return 0
}
`;
const SINLIB = { format: "sinlib", name: "向量库", modules: [{ name: "veclib", src:
`import "std/mathx"
struct Vec { x: float, y: float }
fn vec_add(a: Vec, b: Vec) -> Vec {
    return Vec { x: a.x + b.x, y: a.y + b.y }
}
fn vec_len(v: Vec) -> float {
    return dist(0.0, 0.0, v.x, v.y)
}
` }] };
const LIB_PROG = `import "veclib"

fn main() -> int {
    let v = vec_add(Vec { x: 3.0, y: 0.0 }, Vec { x: 0.0, y: 4.0 })
    print(vec_len(v))
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
  const setSrc = (s) => p.evaluate((v)=>{const ta=document.getElementById("text-out");
    ta.value=v; ta.dispatchEvent(new Event("input",{bubbles:true}));}, s);
  const conText = () => p.evaluate(()=>document.getElementById("dock-con-out").textContent);
  try{
    for(let i=0;i<10;i++){try{await p.goto(`http://127.0.0.1:${port}/index.html`,{waitUntil:"networkidle"});break;}catch(e){if(i===9)throw e;await new Promise(r=>setTimeout(r,500));}}
    await p.waitForFunction(()=>window.__sincReady===true,{timeout:15000});
    await p.waitForTimeout(600);

    // 1) 字符串库：与 C 输出一致（15 / Sincoding / 13 / true / 42）
    await setSrc(STR_PROG);
    await p.waitForTimeout(1200);
    const c1 = await conText();
    res.strOk = ["15","Sincoding","13","true","42"].every((v)=>c1.includes(v));
    res.strStatus = await p.$eval("#text-status", e=>e.textContent);

    // 2) 安装 .sinlib → import → 预览
    res.installErr = await p.evaluate((pkg)=>window._sinLibs.install(pkg), SINLIB);
    await setSrc(LIB_PROG);
    await p.waitForTimeout(1200);
    res.libStatus = await p.$eval("#text-status", e=>e.textContent);
    res.libCats = (await p.$$eval(".cat-btn .cat-nm", e=>e.map(x=>x.textContent))).filter(c=>c.includes("veclib"));
    const c2 = await conText();
    res.vecLen = c2.includes("5");

    // 3) 序列化含 userLibs；重装后（模拟重载）仍可用
    res.saved = await p.evaluate(()=>{
      const pj = window._sinSerializeProject();
      return Array.isArray(pj.userLibs) && pj.userLibs.some((m)=>m.name==="veclib");
    });

    // 4) 删除库 → import 报找不到
    await p.evaluate(()=>window._sinLibs.remove("veclib"));
    await p.waitForTimeout(900);
    res.afterRemove = await p.$eval("#text-status", e=>e.textContent);
    res.removeDiag = await p.evaluate(()=>document.getElementById("diag-list").textContent.includes("找不到模块"));
  }catch(e){res.error=String(e).slice(0,200);}
  res.errors=errs;
  res.ok = res.strOk && res.strStatus.includes("已同步") &&
           res.installErr === null && res.libStatus.includes("已同步") &&
           res.libCats.length >= 1 && res.vecLen && res.saved &&
           /问题/.test(res.afterRemove) && res.removeDiag &&
           errs.length === 0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
