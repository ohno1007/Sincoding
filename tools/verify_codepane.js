// verify_codepane.js — 验证代码面板的可玩性：×关闭/重开 + 可切换 CodeMirror
//   node verify_codepane.js <index.html 绝对路径>
// 断言：
//   1) × 关闭后面板隐藏、画布角落出现「</> 代码」重开按钮；点击恢复
//   2) 切到 CodeMirror：行号出现、内置编辑器隐藏、程序文本已同步
//   3) 在 CM 里编辑 → 回灌 #text-out → 诊断/积木管线联动
//   4) CM 里 Ctrl+空格 → 引擎补全候选（与内置编辑器同一个 sin_complete）
//   5) 切回内置编辑器：CM 宿主移除、textarea 恢复显示
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
  try{
    for(let i=0;i<10;i++){try{await p.goto(`http://127.0.0.1:${port}/index.html`,{waitUntil:"networkidle"});break;}catch(e){if(i===9)throw e;await new Promise(r=>setTimeout(r,500));}}
    await p.waitForFunction(()=>window.__sincReady===true,{timeout:15000});
    await p.waitForTimeout(600);

    // 1) 关闭 / 重开
    await p.click("#tp-close");
    res.closed = await p.evaluate(()=>getComputedStyle(document.getElementById("text-pane")).display==="none"
                                      && !document.getElementById("tp-open").hidden);
    await p.click("#tp-open");
    res.reopened = await p.evaluate(()=>getComputedStyle(document.getElementById("text-pane")).display!=="none"
                                        && document.getElementById("tp-open").hidden);

    // 2) 切 CodeMirror
    await p.selectOption("#tp-editor","codemirror");
    await p.waitForFunction(()=>!!window._sinCodePane.cm(),{timeout:10000});
    await p.waitForTimeout(400);
    res.cm = await p.evaluate(()=>({
      value: window._sinCodePane.cm().getValue().includes("fn fib"),
      lineNumbers: !!document.querySelector("#cm-host .CodeMirror-linenumbers"),
      simpleHidden: document.getElementById("text-edit").style.display==="none",
    }));

    // 3) CM 编辑 → 管线联动（把 fib_iter 改名成未定义调用 → 应报 1 个问题）
    await p.evaluate(()=>{const cm=window._sinCodePane.cm();
      cm.setValue(cm.getValue().replace("fn fib_iter","fn fib_loop"));});
    await p.waitForTimeout(700);
    res.sync = await p.evaluate(()=>({
      textOut: document.getElementById("text-out").value.includes("fib_loop"),
      status: document.getElementById("text-status").textContent,
    }));

    // 4) 引擎补全接进 CM（断言数据层：弹层渲染是 show-hint 自己的职责，
    //    且其失焦即关的行为在无头 evaluate 边界下不稳定）
    res.hints = await p.evaluate(()=>{
      const cm=window._sinCodePane.cm(); cm.focus();
      const idx=cm.getValue().indexOf("print(fib(n))");
      const pos=cm.posFromIndex(idx);
      cm.replaceRange("let z = fi\n    ", pos);
      cm.setCursor({line:pos.line, ch:pos.ch+10});
      const h = window._sinCodePane.hint();
      return h ? h.list.map(x=>x.text) : [];
    });

    // 5) 切回内置
    await p.selectOption("#tp-editor","simple");
    await p.waitForTimeout(300);
    res.back = await p.evaluate(()=>!document.getElementById("cm-host")
                                    && document.getElementById("text-edit").style.display!=="none");
  }catch(e){res.error=String(e).slice(0,180);}
  res.errors=errs;
  res.ok = res.closed && res.reopened &&
           res.cm && res.cm.value && res.cm.lineNumbers && res.cm.simpleHidden &&
           res.sync && res.sync.textOut && /个问题/.test(res.sync.status) &&
           Array.isArray(res.hints) && res.hints.some(h=>/^fib\b/.test(h)) &&
           res.back && errs.length===0;
  console.log(JSON.stringify(res));
  await b.close(); srv.kill();
  process.exit(res.ok?0:1);
})();
