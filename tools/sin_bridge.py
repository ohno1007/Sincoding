#!/usr/bin/env python3
# sin_bridge.py — JSON 接口定义 → 桥接外部 ELF
#
#   python3 sin_bridge.py <module.json> <out_dir>
#
# 读取 JSON 模块定义，产出两样东西：
#   <module>.gen.sin     —— 语言侧 extern fn 声明（供 .sin 程序调用）
#   <module>_bridge.c    —— C 胶水：把我们的 ABI(int=long long,float=double)
#                            转换为外部库真实 C 类型，static 直接调符号、
#                            dynamic 用 dlopen/dlsym 经函数指针调用。
#
# 关键：维护一张 JSON 类型 ↔ 语言类型 ↔ C 类型 的映射表。
import json
import os
import sys

# 三层类型映射表
SIN = {"int": "int", "float": "float", "bool": "bool", "string": "string", "void": "void"}        # 语言类型
ABI = {"int": "long long", "float": "double", "bool": "bool", "string": "const char*", "void": "void"}  # 我们的 C ABI
EXT = {"int": "int", "float": "double", "bool": "bool", "string": "const char*", "void": "void"}        # 外部库真实 C 类型


def check_types(fns):
    for f in fns:
        for t in [f.get("ret", "void")] + [p["type"] for p in f.get("params", [])]:
            if t not in SIN:
                sys.exit(f"未知类型 '{t}'（支持: {', '.join(SIN)}）")


def gen_sin(module, fns):
    lines = [f"// {module}.gen.sin — 由 sin_bridge.py 自动生成的外部模块声明", ""]
    for f in fns:
        ps = ", ".join(f"{p['name']}: {SIN[p['type']]}" for p in f.get("params", []))
        ret = f.get("ret", "void")
        sig = f"extern fn {f['name']}({ps})"
        if ret != "void":
            sig += f" -> {SIN[ret]}"
        lines.append(sig)
    return "\n".join(lines) + "\n"


def _params(fn, typemap):
    return ", ".join(f"{typemap[p['type']]} {p['name']}" for p in fn.get("params", [])) or "void"


def _ext_params_types(fn):
    return ", ".join(EXT[p["type"]] for p in fn.get("params", [])) or "void"


def _call_args(fn):
    # 把 ABI 形参转换为外部 C 类型再传入
    return ", ".join(f"({EXT[p['type']]}){p['name']}" for p in fn.get("params", []))


def gen_bridge_static(module, fns):
    out = [f"// {module}_bridge.c — 静态链接桥接（自动生成）", "#include <stdbool.h>", ""]
    for f in fns:
        ret, sym = f.get("ret", "void"), f["symbol"]
        out.append(f"extern {EXT[ret]} {sym}({_ext_params_types(f)});")
    out.append("")
    for f in fns:
        ret = f.get("ret", "void")
        wrap = f"{ABI[ret]} {f['name']}({_params(f, ABI)})"
        call = f"{f['symbol']}({_call_args(f)})"
        if ret == "void":
            out.append(f"{wrap} {{ {call}; }}")
        else:
            out.append(f"{wrap} {{ return ({ABI[ret]}){call}; }}")
    return "\n".join(out) + "\n"


def gen_bridge_dynamic(module, library, fns):
    out = [
        f"// {module}_bridge.c — 动态加载桥接（dlopen/dlsym，自动生成）",
        "#include <stdbool.h>", "#include <dlfcn.h>", "#include <stdio.h>",
        "#include <stdlib.h>", "",
        f'static void* h_{module} = 0;',
        f"static void ensure_{module}(void) {{",
        f'    if (!h_{module}) h_{module} = dlopen("{library}", RTLD_NOW | RTLD_GLOBAL);',
        f'    if (!h_{module}) {{ fprintf(stderr, "无法加载 {library}: %s\\n", dlerror()); exit(1); }}',
        "}", "",
    ]
    for f in fns:
        ret = f.get("ret", "void")
        fptype = f"{EXT[ret]} (*)({_ext_params_types(f)})"
        wrap = f"{ABI[ret]} {f['name']}({_params(f, ABI)})"
        out.append(f"{wrap} {{")
        out.append(f"    static {EXT[ret]} (*fp)({_ext_params_types(f)}) = 0;")
        out.append(f"    ensure_{module}();")
        out.append(f'    if (!fp) fp = ({fptype})dlsym(h_{module}, "{f["symbol"]}");')
        call = f"fp({_call_args(f)})"
        if ret == "void":
            out.append(f"    {call};")
        else:
            out.append(f"    return ({ABI[ret]}){call};")
        out.append("}")
    return "\n".join(out) + "\n"


def main():
    if len(sys.argv) != 3:
        print("用法: sin_bridge.py <module.json> <out_dir>", file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8") as f:
        spec = json.load(f)
    outdir = sys.argv[2]
    os.makedirs(outdir, exist_ok=True)

    module = spec["module"]
    link = spec.get("link", "static")
    library = spec.get("library", f"lib{module}.so")
    fns = spec["functions"]
    check_types(fns)

    sin_path = os.path.join(outdir, f"{module}.gen.sin")
    bridge_path = os.path.join(outdir, f"{module}_bridge.c")
    with open(sin_path, "w", encoding="utf-8") as f:
        f.write(gen_sin(module, fns))
    with open(bridge_path, "w", encoding="utf-8") as f:
        f.write(gen_bridge_static(module, fns) if link == "static"
                else gen_bridge_dynamic(module, library, fns))

    # 输出构建信息（供 build_with_bridge.sh 读取）
    print(f"MODULE={module}")
    print(f"LINK={link}")
    print(f"LIBRARY={library}")
    print(f"GEN_SIN={sin_path}")
    print(f"BRIDGE_C={bridge_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
