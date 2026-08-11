// runtime.h — Sincoding 运行时库（Scratch 风格 API，封装 raylib）
//
// 设计原则：用户程序不直接调用 raylib，而是调用这层 rt_* API。
// 用户写 move(sprite, 10)，转出的 C 调用 rt_move()，内部再调 raylib。
// 好处：以后换底层渲染库（如 SDL2）只改 runtime，用户程序不受影响。
//
// 注意：本文件依赖 raylib，对应开发路线图「阶段 2」。
// 阶段 1（语言 → C 转译器）已可独立验证，无需本库。
#ifndef SINCODING_RUNTIME_H
#define SINCODING_RUNTIME_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------- 舞台 / 主循环（绿旗运行） ----------
// 对应 raylib 的 InitWindow + while(!WindowShouldClose())
void rt_stage_init(int width, int height, const char* title);
bool rt_stage_running(void);      // 主循环条件
void rt_frame_begin(void);        // 每帧开始：BeginDrawing + 清屏
void rt_frame_end(void);          // 每帧结束：EndDrawing
void rt_stage_close(void);        // 关闭窗口、释放资源

// ---------- 角色（精灵） ----------
// 内部为 Texture2D + 位置/旋转/缩放，返回句柄 id
typedef int rt_sprite;
rt_sprite rt_sprite_load(const char* image_path);
// 程序化方块角色：无需图片资源，适合最小垂直切片 / 占位。
rt_sprite rt_sprite_rect(float x, float y, float size);
void rt_sprite_draw(rt_sprite s);

// 移动 / 旋转 / 定位（Scratch 积木的直接映射）
void rt_move(rt_sprite s, float steps);     // 沿当前朝向前进
void rt_goto(rt_sprite s, float x, float y);
void rt_turn(rt_sprite s, float degrees);
void rt_point(rt_sprite s, float degrees);  // 面向指定方向
float rt_x(rt_sprite s);
float rt_y(rt_sprite s);
bool rt_touching(rt_sprite a, rt_sprite b);   // 两精灵 AABB 是否重叠（碰撞检测）

// ---------- 输入 ----------
bool rt_key_down(int key);
bool rt_key_pressed(int key);              // 本帧刚按下（边沿触发）
bool rt_mouse_clicked(void);               // 本帧刚点下左键
void rt_show(rt_sprite s);
void rt_hide(rt_sprite s);
void rt_bounce(rt_sprite s);               // 碰到舞台边缘就反弹
bool rt_touching_mouse(rt_sprite s);       // 精灵是否碰到鼠标指针
void rt_costume(rt_sprite s, const char* path);   // 切换造型（同名纹理缓存）
double rt_timer(void);                     // 舞台启动起的秒数
void rt_timer_reset(void);        // 对应 IsKeyDown
void rt_wait(float secs);                  // 阻塞等待（Scratch 的「等待 x 秒」）
bool rt_mouse_down(int button);
float rt_mouse_x(void);
float rt_mouse_y(void);

// ---------- 平台 / 工具 ----------
int rt_random(int lo, int hi);    // [lo, hi] 闭区间随机整数
int rt_screen_w(void);
int rt_screen_h(void);
long long rt_frame_index(void);   // 已渲染帧数（计时/动画用）

// ---------- 画笔（持久绘制层，跨帧保留，类似 Scratch 画笔） ----------
void rt_pen_clear(void);
void rt_pen_color(int r, int g, int b);
void rt_pen_size(float w);
void rt_pen_line(float x1, float y1, float x2, float y2);
void rt_pen_dot(float x, float y);

// ---------- 外观 ----------
void rt_say(rt_sprite s, const char* text);  // 气泡：DrawText
void rt_set_scale(rt_sprite s, float scale);
void rt_draw_text(const char* text, float x, float y, int size); // 在舞台坐标处画文字
void rt_draw_int(long long n, float x, float y, int size);       // 在舞台坐标处画整数

// ---------- 声音 ----------
typedef int rt_sound;
rt_sound rt_sound_load(const char* path);
void rt_sound_play(rt_sound snd);
void rt_play_tone(int freq, int ms);   // 简易蜂鸣（无音频设备时为空操作）

// ---------- 广播 / 事件（自建事件队列） ----------
void rt_broadcast(const char* message);
bool rt_received(const char* message);   // 本帧是否收到该广播

// 常用按键码（与 raylib 对齐，避免用户接触 raylib 头文件）
enum {
    RT_KEY_RIGHT = 262,
    RT_KEY_LEFT  = 263,
    RT_KEY_DOWN  = 264,
    RT_KEY_UP    = 265,
    RT_KEY_SPACE = 32,
};

#ifdef __cplusplus
}
#endif

#endif // SINCODING_RUNTIME_H
