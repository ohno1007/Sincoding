#!/usr/bin/env python3
# ide_server.py —— 积木编辑器的本地构建服务
#
#   python3 tools/ide_server.py [port]
#   然后浏览器打开 http://127.0.0.1:<port>/index.html
#
# 1) 静态托管 editor/（含 wasm 反向同步、造型 assets/）。
# 2) 暴露 POST /api/publish：接收编辑器「发布」模态框的 {name,pkg,platforms,source,
#    logo,assets}，调用既有 tools/build_*.sh 把项目一键交叉编译到 Linux / Windows /
#    Android / Web，产物写入 editor/dist/<name>/<platform>/，并返回每个平台的结果
#    （含可下载/打开的相对链接，editor 自身就托管 dist/）。
#
# 浏览器不能交叉编译，所有真正的编译都在本机由这些脚本完成。
import base64
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EDITOR = os.path.join(ROOT, "editor")
TOOLS = os.path.join(ROOT, "tools")
DIST = os.path.join(EDITOR, "dist")            # 放在 editor 下，便于浏览器直接下载/打开


def _slug(s):
    s = re.sub(r"[^A-Za-z0-9_.-]+", "_", (s or "").strip()) or "game"
    return s.strip("_") or "game"


def _decode_data_url(data_url):
    """data:[<mime>][;base64],<data> → bytes"""
    if not data_url:
        return None
    m = re.match(r"data:[^,]*;base64,(.*)$", data_url, re.S)
    if m:
        return base64.b64decode(m.group(1))
    m = re.match(r"data:[^,]*,(.*)$", data_url, re.S)
    if m:
        return m.group(1).encode()
    return None


def _write_assets(assets, dest):
    """assets: {filename: dataURL} → 写入 dest 目录，返回是否有资源"""
    os.makedirs(dest, exist_ok=True)
    n = 0
    for name, url in (assets or {}).items():
        raw = _decode_data_url(url)
        if raw is None:
            continue
        with open(os.path.join(dest, os.path.basename(name)), "wb") as f:
            f.write(raw)
        n += 1
    return n > 0


def _run(cmd, env=None, cwd=None, timeout=900):
    e = dict(os.environ)
    if env:
        e.update(env)
    p = subprocess.run(cmd, cwd=cwd or ROOT, env=e,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    return p.returncode, p.stdout.decode("utf-8", "replace")


def _have(*paths):
    return all(os.path.exists(p) for p in paths)


def build_platform(plat, src, name, pkg, work, assets_dir, has_assets, logo_path):
    """编译单个平台；返回 (ok, artifact_rel_or_None, error_or_None, log)"""
    slug = _slug(name)
    outdir = os.path.join(DIST, slug, plat)
    os.makedirs(outdir, exist_ok=True)
    sh = lambda n: os.path.join(TOOLS, n)

    if plat == "linux":
        out = os.path.join(outdir, slug)
        rc, log = _run([sh("build_native.sh"), src, out])
        if rc == 0 and has_assets:
            for f in os.listdir(assets_dir):
                shutil.copy(os.path.join(assets_dir, f), outdir)
        rel = os.path.relpath(out, EDITOR) if rc == 0 else None
        return (rc == 0, rel, None if rc == 0 else "build_native 失败", log)

    if plat == "web":
        rc, log = _run([sh("build_web.sh"), src, outdir] + ([assets_dir] if has_assets else []))
        rel = os.path.relpath(os.path.join(outdir, "index.html"), EDITOR) if rc == 0 else None
        return (rc == 0, rel, None if rc == 0 else "build_web 失败（需 emscripten）", log)

    if plat == "windows":
        out = os.path.join(outdir, slug + ".exe")
        rc, log = _run([sh("build_windows.sh"), src, out])
        if rc == 0 and has_assets:
            for f in os.listdir(assets_dir):
                shutil.copy(os.path.join(assets_dir, f), outdir)
        rel = os.path.relpath(out, EDITOR) if rc == 0 else None
        return (rc == 0, rel, None if rc == 0 else "build_windows 失败（需 MinGW-w64）", log)

    if plat == "android":
        out = os.path.join(outdir, slug + ".apk")
        env = {"ANDROID_PKG": pkg or "org.sincoding.game"}
        if has_assets:
            env["ANDROID_ASSETS"] = assets_dir
        if logo_path:
            env["ANDROID_ICON"] = logo_path
        ndk = os.environ.get("ANDROID_NDK", "/usr/lib/android-ndk")
        sdk = os.environ.get("ANDROID_SDK_ROOT", os.environ.get("ANDROID_HOME", "/tmp/android-sdk"))
        env.setdefault("ANDROID_NDK", ndk)
        env.setdefault("ANDROID_SDK_ROOT", sdk)
        rc, log = _run([sh("build_apk.sh"), src, out, name], env=env)
        rel = os.path.relpath(out, EDITOR) if rc == 0 else None
        return (rc == 0, rel, None if rc == 0 else "build_apk 失败（需 NDK + SDK build-tools）", log)

    return (False, None, "未知平台: " + plat, "")


def do_publish(payload):
    name = payload.get("name") or "Game"
    pkg = payload.get("pkg") or "org.sincoding.game"
    platforms = payload.get("platforms") or []
    source = payload.get("source") or ""
    assets = payload.get("assets") or {}
    logo = payload.get("logo")

    work = tempfile.mkdtemp(prefix="sinpub_")
    try:
        src = os.path.join(work, "program.sin")
        with open(src, "w") as f:
            f.write(source)
        # .sinlib 用户库：落盘成 <模块名>.sin 放在源码旁——sinc 的 import 解析
        # 会先查同目录，浏览器里装的库因此在原生编译时同样可见
        for lib in (payload.get("libs") or []):
            lname, lsrc = lib.get("name"), lib.get("src")
            if not lname or not isinstance(lsrc, str):
                continue
            if ".." in lname or lname.startswith("/"):   # 路径穿越防护
                continue
            lpath = os.path.join(work, lname + ".sin")   # 模块名可含子目录（如 mylib/vec）
            os.makedirs(os.path.dirname(lpath) or work, exist_ok=True)
            with open(lpath, "w") as f:
                f.write(lsrc)
        assets_dir = os.path.join(work, "assets")
        has_assets = _write_assets(assets, assets_dir)
        logo_path = None
        if logo:
            raw = _decode_data_url(logo)
            if raw:
                logo_path = os.path.join(work, "logo.png")
                with open(logo_path, "wb") as f:
                    f.write(raw)
                # logo 也作为资源给桌面/web（可被 sprite_load("logo.png") 使用）
                if not has_assets:
                    os.makedirs(assets_dir, exist_ok=True)
                shutil.copy(logo_path, os.path.join(assets_dir, "logo.png"))
                has_assets = True

        results = []
        for plat in platforms:
            try:
                ok, rel, err, log = build_platform(plat, src, name, pkg, work,
                                                   assets_dir, has_assets, logo_path)
            except subprocess.TimeoutExpired:
                ok, rel, err, log = False, None, "编译超时", ""
            except Exception as ex:  # noqa: BLE001
                ok, rel, err, log = False, None, str(ex), ""
            results.append({"platform": plat, "ok": ok, "artifact": rel,
                            "error": err, "log": (log or "")[-4000:]})
        return {"ok": all(r["ok"] for r in results), "name": name, "results": results}
    finally:
        shutil.rmtree(work, ignore_errors=True)


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=EDITOR, **k)

    def log_message(self, *a):  # 安静
        pass

    def do_POST(self):
        if self.path.rstrip("/") != "/api/publish":
            self.send_error(404); return
        try:
            n = int(self.headers.get("Content-Length", 0))
            payload = json.loads(self.rfile.read(n).decode("utf-8"))
        except Exception as ex:  # noqa: BLE001
            self.send_error(400, "bad json: %s" % ex); return
        try:
            out = do_publish(payload)
            body = json.dumps(out).encode("utf-8")
        except Exception as ex:  # noqa: BLE001
            self.send_response(500)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"ok": False, "error": str(ex)}).encode())
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    os.makedirs(DIST, exist_ok=True)
    httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print("Sincoding IDE + 构建服务: http://127.0.0.1:%d/index.html" % port)
    print("发布产物目录: %s" % DIST)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
