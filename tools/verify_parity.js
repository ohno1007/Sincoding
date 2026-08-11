// verify_parity.js — 「预览 = 成品」批的浏览器端验证
//   node verify_parity.js <index.html 绝对路径> <parity.sin 路径> [原生输出文件]
// 断言：
//   1) parity.sin 在预览里的控制台输出 == 原生成品的 stdout（逐行）
//      （运动方向 0°朝右/90°朝上、整数/浮点除法、%g 打印格式全部对齐）
//   2) 整数除零：预览报「第N行: 除数为 0」（与生成的 C 的 sin_div 同语义）
//   3) 点绿旗后画布自动聚焦（按键立即生效，不用再点一下画面）
//   4) 键盘全键映射：字母 a/A→65、数字 5→53（与 raylib 键码一致）
//   5) 拖出积木默认变量自动接线：sprite_move 的 s → 作用域里的 hero；x=x+1 的 x → score
//   6) 模板载入替换整个项目：旧精灵一并清掉，不再混跑
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const fs = require("fs");
const net = require("net");
const path = require("path");
function freePort(){return new Promise(r=>{const s=net.createServer();s.listen(0,"127.0.0.1",()=>{const p=s.address().port;s.close(()=>r(p));});});}

(async () => {
  const htmlPath = process.argv[2], sinPath = process.argv[3], expPath = process.argv[4];
  if (!htmlPath || !sinPath) { console.error("用法: verify_parity.js <index.html> <parity.sin> [原生输出]"); process.exit(2); }
  const parSrc = fs.readFileSync(sinPath, "utf8");
  // 期望输出：优先用原生成品跑出来的 stdout（对测的意义所在）；没有就用固化基线
  const expected = (expPath ? fs.readFileSync(expPath, "utf8")
    : "1000\n0\n0\n1000\n-707\n707\n3\n-3\n1\n3.5\n0.333333\n2.5|5\n8\n4\ntrue\n")
    .split("\n").map(s=>s.trim()).filter(s=>s.length);
  const port = await freePort();
  const srv = spawn("python3",["-m","http.server",String(port),"--bind","127.0.0.1","--directory",path.dirname(htmlPath)],{stdio:"ignore"});
  const b = await chromium.launch();
  const p = await b.newPage({viewport:{width:1440,height:900}});
  const errs=[]; p.on("pageerror",e=>errs.push(e.message));
  const res={};
  const setSrc = (s) => p.evaluate((v)=>{const ta=document.getElementById("text-out");
    ta.value=v; ta.dispatchEvent(new Event("input",{bubbles:true}));}, s);
  const pvStatus = () => p.$eval("#pv-status", e=>e.textContent);
  try{
    for(let i=0;i<10;i++){try{await p.goto(`http://127.0.0.1:${port}/index.html`,{waitUntil:"networkidle"});break;}catch(e){if(i===9)throw e;await new Promise(r=>setTimeout(r,500));}}
    await p.waitForFunction(()=>window.__sincReady===true,{timeout:15000});
    await p.waitForTimeout(600);

    // 1) 对测：同一份 parity.sin，预览控制台 == 原生 stdout
    // 默认项目是 3 个演示精灵——先裁到单精灵，让预览跑的就是这一份程序
    await p.evaluate(()=>{ const pj=window._sin.project; pj.sprites.splice(1); pj.cur=0; window._sin.render(); });
    await setSrc(parSrc);
    await p.waitForFunction(()=>document.getElementById("pv-status").textContent.includes("运行完成"),{timeout:8000});
    await p.waitForTimeout(400);
    const got = await p.evaluate(()=>window._sinPreview.world.console.slice());
    res.parityGot = got; res.parityWant = expected;
    res.parity = got.length === expected.length && got.every((v,i)=>String(v).trim()===expected[i]);

    // 1b) 引擎回写往返：积木模型 → 源码 仍保留 break/continue/元素字段赋值/wait
    await p.evaluate(()=>window._sin.refreshText());
    await p.waitForTimeout(700);
    res.roundtrip = await p.evaluate(()=>{
      const t = document.getElementById("text-out").value;
      return t.includes("break") && t.includes("continue") &&
             t.includes("foes[0].hp = 7") && t.includes("wait(");
    });
    res.roundtripStatus = await p.$eval("#text-status", e=>e.textContent);

    // 2) 整数除零：与成品同语义的中文行号报错
    await setSrc('fn main() -> int {\n    let n = 0\n    print(10 / n)\n    return 0\n}\n');
    await p.waitForFunction(()=>document.getElementById("pv-status").textContent.includes("运行出错"),{timeout:8000});
    res.divzeroMsg = await pvStatus();
    res.divzero = /第3行/.test(res.divzeroMsg) && /除数为 0/.test(res.divzeroMsg);

    // 3) 点绿旗 → 画布聚焦
    await p.evaluate(()=>document.getElementById("preview-canvas").blur());
    await p.click("#pv-run");
    await p.waitForTimeout(200);
    res.focus = await p.evaluate(()=>document.activeElement && document.activeElement.id === "preview-canvas");

    // 4) 键盘全键映射（raylib 键码：A=65、5=53；大小写同键）
    res.keymap = await p.evaluate(()=>{
      const c = document.getElementById("preview-canvas"), k = window._sinPreview.keys;
      c.dispatchEvent(new KeyboardEvent("keydown",{key:"a",bubbles:true}));
      c.dispatchEvent(new KeyboardEvent("keydown",{key:"5",bubbles:true}));
      const down = k.has(65) && k.has(53);
      c.dispatchEvent(new KeyboardEvent("keyup",{key:"A",bubbles:true}));
      c.dispatchEvent(new KeyboardEvent("keyup",{key:"5",bubbles:true}));
      return down && !k.has(65) && !k.has(53);
    });

    // 5) 拖出积木默认变量自动接线
    await setSrc('fn on_frame() {\n    let hero = sprite_new(0.0, 0.0, 40.0)\n    let score = 0\n}\n');
    await p.waitForTimeout(800);   // 等解析落地（text-status 可能残留上一步的「已同步」，不能拿来等）
    await p.evaluate(()=>{
      const click = (sel)=>{
        const w = document.querySelector(sel);
        w.dispatchEvent(new PointerEvent("pointerdown",{button:0,bubbles:true}));
        window.dispatchEvent(new PointerEvent("pointerup",{bubbles:true}));
      };
      click('.pal-wys[data-kind="sprite_move"]');
      click('.pal-wys[data-kind="incr"]');
    });
    await p.waitForTimeout(300);
    res.fixvars = await p.evaluate(()=>{
      const body = window._sin.project.sprites[window._sin.project.cur].program[0].body;
      const mv = body.find(s=>s.block==="expr" && s.expr.callee==="sprite_move");
      const inc = body.find(s=>s.block==="assign" && s.value && s.value.block==="binary");
      return !!mv && mv.expr.args[0].name === "hero" &&
             !!inc && inc.name === "score" && inc.value.lhs.name === "score";
    });

    // 6) 模板载入替换整个项目（旧精灵清掉，不再混跑）
    await p.evaluate(()=>{
      const pj = window._sin.project;
      pj.sprites.push({name:"残留精灵",icon:"🎭",program:[{block:"fn",name:"update",params:[],ret:"void",body:[]}],costumes:[]});
    });
    await p.evaluate(()=>window._sinTemplates.load("bounce"));
    await p.waitForTimeout(900);
    res.template = await p.evaluate(()=>{
      const pj = window._sin.project;
      return pj.sprites.length === 1 && pj.sprites[0].program.length > 0 &&
             document.getElementById("pv-status").textContent.length > 0;
    });
    res.templateStatus = await p.$eval("#text-status", e=>e.textContent);
  }catch(e){res.error=String(e).slice(0,300);}
  res.errors=errs;
  res.ok = res.parity && res.roundtrip && /已同步/.test(res.roundtripStatus||"") &&
           res.divzero && res.focus && res.keymap && res.fixvars &&
           res.template && /已同步|问题/.test(res.templateStatus||"") && errs.length===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
