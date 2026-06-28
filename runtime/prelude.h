// prelude.h — 语言内建/外部函数桥接层
//
// 这是 Sincoding 语言看到的「标准库」。用户在 .sin 里用
//   extern fn stage_init(w: int, h: int)
// 声明这些函数，转出的 C 直接调用它们；本层再转调 runtime（封装 raylib）。
//
// ABI 约定（与编译器 codegen 对齐）：
//   语言 int   → C long long
//   语言 float → C double
//   语言 bool  → C bool
#ifndef SINCODING_PRELUDE_H
#define SINCODING_PRELUDE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 舞台 / 主循环
void stage_init(long long w, long long h);
bool stage_running(void);
void frame_begin(void);
void frame_end(void);
void stage_close(void);

// 方块角色（无需图片资源）
long long sprite_new(double x, double y, double size);
// 从 PNG 造型加载纹理角色（造型画板导出的 PNG 即可直接用）
long long sprite_load(const char* path);
void sprite_move_to(long long s, double x, double y);
double sprite_x(long long s);
double sprite_y(long long s);
void sprite_draw(long long s);

// 文字 / 气泡（string 接入运行时）
void say(long long s, const char* text);
void draw_text(const char* text, double x, double y, long long size);
void draw_number(long long n, double x, double y, long long size);

// 输入
bool key_down(long long key);
long long key_left(void);
long long key_right(void);
long long key_up(void);
long long key_down_arrow(void);

// 声音
long long sound_load(const char* path);
void play_sound(long long snd);
void play_tone(long long freq, long long ms);

// 广播 / 事件
void broadcast(const char* message);
bool received(const char* message);

#ifdef __cplusplus
}
#endif

#endif // SINCODING_PRELUDE_H
