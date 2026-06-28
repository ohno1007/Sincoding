// verify_palette_blocks.js —— 把调色板新增积木的形状全部用上，序列化成 .sin
//   node verify_palette_blocks.js <out.sin>
// 由 tests 编译该 .sin，验证「每个积木产出的文本都能被规范引擎解析+编译」。
const fs = require("fs");
const path = require("path");

const root = {};
const code = fs.readFileSync(path.resolve(__dirname, "../editor/blockmodel.js"), "utf8");
new Function("root", code).call(root, root);
const BlockModel = root.BlockModel;

const I = (v) => ({ block: "int", value: v });
const F = (v) => ({ block: "float", value: v });
const S = (v) => ({ block: "string", value: v });
const Vr = (n) => ({ block: "var", name: n });
const C = (c, ...a) => ({ block: "call", callee: c, args: a });
const Bn = (o, l, r) => ({ block: "binary", op: o, lhs: l, rhs: r });
const Ex = (e) => ({ block: "expr", expr: e });
const L = (name, type, value, len = 0) => ({ block: "let", name, type, len, value });

const body = [
  Ex(C("stage_init", I(800), I(600))),
  L("s", "int", C("sprite_load", S("ball.png"))),
  L("h", "int", C("sprite_new", F(0), F(0), F(40))),
  L("x", "int", I(0)),
  { block: "assign", name: "x", value: Bn("+", Vr("x"), I(1)) },
  { block: "assign", name: "x", value: Bn("-", Vr("x"), I(1)) },
  L("n", "int", C("to_int", F(0))),
  L("f", "float", C("to_float", I(0))),
  Ex(C("sprite_move_to", Vr("s"), F(0), F(0))),
  L("px", "float", C("sprite_x", Vr("s"))),
  L("py", "float", C("sprite_y", Vr("s"))),
  { block: "while", cond: C("stage_running"), body: [
    Ex(C("frame_begin")),
    { block: "if", cond: C("key_down", C("key_left")), then: [Ex(C("sprite_move_to", Vr("s"), Vr("px"), Vr("py")))] },
    { block: "if", cond: C("key_down", C("key_right")), then: [] },
    { block: "if", cond: C("key_down", C("key_up")), then: [] },
    { block: "if", cond: C("key_down", C("key_down_arrow")), then: [] },
    { block: "if", cond: C("received", S("go")), then: [Ex(C("broadcast", S("go")))] },
    { block: "if", cond: { block: "bool", value: true }, then: [Ex(C("play_tone", I(440), I(200)))], else: [Ex(C("play_sound", I(0)))] },
    Ex(C("sprite_draw", Vr("s"))),
    Ex(C("say", Vr("s"), S("你好"))),
    Ex(C("draw_text", S("文字"), F(0), F(0), I(24))),
    Ex(C("draw_number", Vr("x"), F(0), F(0), I(24))),
    Ex(C("frame_end")),
  ] },
  Ex(C("stage_close")),
  { block: "return", value: I(0) },
];
const model = { structs: [], globals: [], program: [{ block: "fn", name: "main", params: [], ret: "int", body }] };
const externs = [
  "extern fn stage_init(w: int, h: int)", "extern fn stage_running() -> bool",
  "extern fn frame_begin()", "extern fn frame_end()", "extern fn stage_close()",
  "extern fn sprite_new(x: float, y: float, size: float) -> int",
  "extern fn sprite_load(path: string) -> int", "extern fn sprite_move_to(s: int, x: float, y: float)",
  "extern fn sprite_x(s: int) -> float", "extern fn sprite_y(s: int) -> float", "extern fn sprite_draw(s: int)",
  "extern fn key_down(key: int) -> bool", "extern fn key_left() -> int", "extern fn key_right() -> int",
  "extern fn key_up() -> int", "extern fn key_down_arrow() -> int", "extern fn say(s: int, text: string)",
  "extern fn draw_text(text: string, x: float, y: float, size: int)", "extern fn draw_number(n: int, x: float, y: float, size: int)",
  "extern fn play_sound(snd: int)", "extern fn play_tone(freq: int, ms: int)",
  "extern fn broadcast(message: string)", "extern fn received(message: string) -> bool",
  "extern fn to_float(n: int) -> float", "extern fn to_int(f: float) -> int",
];
const src = externs.join("\n") + "\n\n" + BlockModel.modelToSource(model);
const out = process.argv[2] || "/tmp/palette_blocks.sin";
fs.writeFileSync(out, src);
console.log("wrote", out);
