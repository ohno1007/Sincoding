// prelude.c — 语言桥接层实现，转调 runtime（封装 raylib）
//
// 同时内置「无头测试钩子」，便于 CI 在无显示器环境下验证整条流水线：
//   SIN_MAX_FRAMES=N   运行 N 帧后自动结束主循环
//   SIN_SCREENSHOT=f   在结束前一帧把画面截图保存为 f（PNG）
#include "prelude.h"
#include "runtime.h"

#include <stdlib.h>

#include "raylib.h"

static long long g_frame = 0;
static long long g_max_frames = 0;     // 0 = 不限制
static const char* g_screenshot = NULL;

void stage_init(long long w, long long h) {
    const char* mf = getenv("SIN_MAX_FRAMES");
    if (mf) g_max_frames = atoll(mf);
    g_screenshot = getenv("SIN_SCREENSHOT");
    rt_stage_init((int)w, (int)h, "Sincoding");
}

bool stage_running(void) {
    if (g_max_frames > 0 && g_frame >= g_max_frames) return false;
    return rt_stage_running();
}

void frame_begin(void) { rt_frame_begin(); }

void frame_end(void) {
    rt_frame_end(); // EndDrawing：刷新绘制批次并交换缓冲
    // 截图须在 EndDrawing 之后读取已呈现的帧缓冲
    if (g_screenshot && g_max_frames > 0 && g_frame == g_max_frames - 1)
        TakeScreenshot(g_screenshot);
    g_frame++;
}

void stage_close(void) { rt_stage_close(); }

long long sprite_new(double x, double y, double size) {
    return (long long)rt_sprite_rect((float)x, (float)y, (float)size);
}

long long sprite_load(const char* path) {
    return (long long)rt_sprite_load(path);
}

void say(long long s, const char* text) { rt_say((rt_sprite)s, text); }

void draw_text(const char* text, double x, double y, long long size) {
    rt_draw_text(text, (float)x, (float)y, (int)size);
}

void draw_number(long long n, double x, double y, long long size) {
    rt_draw_int(n, (float)x, (float)y, (int)size);
}

void sprite_move_to(long long s, double x, double y) {
    rt_goto((rt_sprite)s, (float)x, (float)y);
}

double sprite_x(long long s) { return (double)rt_x((rt_sprite)s); }
double sprite_y(long long s) { return (double)rt_y((rt_sprite)s); }
void sprite_draw(long long s) { rt_sprite_draw((rt_sprite)s); }

bool key_down(long long key) { return rt_key_down((int)key); }

long long key_left(void)       { return RT_KEY_LEFT; }
long long key_right(void)      { return RT_KEY_RIGHT; }
long long key_up(void)         { return RT_KEY_UP; }
long long key_down_arrow(void) { return RT_KEY_DOWN; }

long long sound_load(const char* path) { return (long long)rt_sound_load(path); }
void play_sound(long long snd) { rt_sound_play((rt_sound)snd); }
void play_tone(long long freq, long long ms) { rt_play_tone((int)freq, (int)ms); }

void broadcast(const char* message) { rt_broadcast(message); }
bool received(const char* message) { return rt_received(message); }
