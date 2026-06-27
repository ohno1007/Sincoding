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
void sprite_move_to(long long s, double x, double y);
double sprite_x(long long s);
double sprite_y(long long s);
void sprite_draw(long long s);

// 输入
bool key_down(long long key);
long long key_left(void);
long long key_right(void);
long long key_up(void);
long long key_down_arrow(void);

#ifdef __cplusplus
}
#endif

#endif // SINCODING_PRELUDE_H
