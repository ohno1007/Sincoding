// runtime.c — Scratch 风格运行时，基于 raylib 实现
//
// 这是开发路线图「阶段 2」的核心库。它把 Scratch 概念映射到 raylib：
//   舞台/绿旗  → InitWindow + 主循环
//   角色       → Texture2D + 位置/旋转/缩放
//   移动/旋转  → 改坐标 + DrawTextureEx
//   按键       → IsKeyDown
//   广播/事件  → 自建事件队列
//   声音       → PlaySound
//   说/想气泡  → DrawText
//
// 编译需链接 raylib，例如：
//   gcc your_program.c runtime.c -lraylib -lm -o game
#include "runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#define RT_PI 3.14159265358979323846f
#define RT_MAX_SPRITES 256
#define RT_MAX_SOUNDS  128
#define RT_MAX_EVENTS  64
#define RT_MSG_LEN     64

typedef enum { RT_SPR_TEXTURE, RT_SPR_RECT } RtSpriteKind;

typedef struct {
    RtSpriteKind kind;
    Texture2D tex;     // RT_SPR_TEXTURE 时有效
    bool loaded;
    float size;        // RT_SPR_RECT 时的边长
    float x, y;        // 舞台坐标（中心原点，y 向上为正，贴近 Scratch）
    float heading;     // 朝向，度，0 = 向右
    float scale;
    bool hidden;       // 隐藏时不绘制、不参与碰撞
    char bubble[128];  // 说的气泡，空串表示不显示
} RtSprite;

static RtSprite g_sprites[RT_MAX_SPRITES];
static int g_sprite_count = 0;

static Sound g_sounds[RT_MAX_SOUNDS];
static bool g_sound_loaded[RT_MAX_SOUNDS];
static int g_sound_count = 0;

// 双缓冲事件队列：本帧投递到 next，下一帧交换到 cur 供查询
static char g_events_cur[RT_MAX_EVENTS][RT_MSG_LEN];
static int g_events_cur_n = 0;
static char g_events_next[RT_MAX_EVENTS][RT_MSG_LEN];
static int g_events_next_n = 0;

static int g_stage_w = 800, g_stage_h = 600;
static bool g_audio_inited = false;
static long long g_frame_index = 0;

// 画笔持久层：跨帧保留的离屏画布（RenderTexture），每帧开始时贴回屏幕
static RenderTexture2D g_pen;
static bool g_pen_ready = false;
static Color g_pen_color = { 0, 0, 0, 255 };
static float g_pen_size = 2.0f;

// 舞台坐标 → 屏幕坐标（中心原点、y 向上 → 左上原点、y 向下）
static Vector2 stage_to_screen(float sx, float sy) {
    Vector2 v;
    v.x = sx + (float)g_stage_w * 0.5f;
    v.y = (float)g_stage_h * 0.5f - sy;
    return v;
}

// ---------- 舞台 / 主循环 ----------
#ifdef SIN_DEBUG
// 调试面板（debug_overlay.cpp，仅 --debug 构建链接）
void sin_dbg_init(void);
void sin_dbg_shutdown(void);
void sin_dbg_draw(int spriteCount, const float* sx, const float* sy);
void sin_dbg_frame_done(void);
bool sin_dbg_blocked(void);
#endif

void rt_stage_init(int width, int height, const char* title) {
    g_stage_w = width;
    g_stage_h = height;
    InitWindow(width, height, title ? title : "Sincoding");
    InitAudioDevice();
    g_audio_inited = true;
    SetTargetFPS(60);
    // 画笔持久层（透明底）
#ifdef SIN_DEBUG
    sin_dbg_init();
#endif
    g_pen = LoadRenderTexture(width, height);
    BeginTextureMode(g_pen);
    ClearBackground(BLANK);
    EndTextureMode();
    g_pen_ready = true;
    g_frame_index = 0;
}

// 帧内 stage_close 的语义是「请求退出」：真正的 CloseWindow 必须等这一帧画完
// （EndDrawing 之前关窗会段错误），也要防重复 close（事件驱动的合成驱动会在
// 循环后再补一次 stage_close）。
static bool g_quit_requested = false;
static bool g_in_frame = false;
static bool g_closed = false;

bool rt_stage_running(void) {
    return !g_quit_requested && !WindowShouldClose();
}

void rt_frame_begin(void) {
    g_in_frame = true;
    // 交换事件队列：上一帧投递的广播在本帧可被查询
    memcpy(g_events_cur, g_events_next, sizeof(g_events_cur));
    g_events_cur_n = g_events_next_n;
    g_events_next_n = 0;

    BeginDrawing();
    ClearBackground(RAYWHITE);
    // 贴回画笔持久层（RenderTexture 的 y 轴与屏幕相反，源矩形高取负翻转）
    if (g_pen_ready) {
        Rectangle src = { 0, 0, (float)g_pen.texture.width, -(float)g_pen.texture.height };
        DrawTextureRec(g_pen.texture, src, (Vector2){ 0, 0 }, WHITE);
    }
}

void rt_frame_end(void) {
#ifdef SIN_DEBUG
    // 调试面板画在最上层（EndDrawing 之前），并把精灵坐标喂给「精灵」检查器
    {
        static float sx[RT_MAX_SPRITES], sy[RT_MAX_SPRITES];
        for (int i = 0; i < g_sprite_count; i++) { sx[i] = g_sprites[i].x; sy[i] = g_sprites[i].y; }
        sin_dbg_draw(g_sprite_count, sx, sy);
    }
#endif
    EndDrawing();
    g_in_frame = false;
    g_frame_index++;
#ifdef SIN_DEBUG
    sin_dbg_frame_done();
    // 「暂停」时原地空转（仍持续绘制面板），使画面冻结但 UI 可交互
    while (sin_dbg_blocked() && !WindowShouldClose()) {
        BeginDrawing();
        sin_dbg_draw(g_sprite_count, NULL, NULL);
        EndDrawing();
    }
#endif
}

void rt_stage_close(void) {
    if (g_in_frame) { g_quit_requested = true; return; }   // 帧内：只请求退出
    if (g_closed) return;                                  // 已关过：幂等
    g_closed = true;
#ifdef SIN_DEBUG
    sin_dbg_shutdown();
#endif
    for (int i = 0; i < g_sprite_count; i++)
        if (g_sprites[i].loaded) UnloadTexture(g_sprites[i].tex);
    for (int i = 0; i < g_sound_count; i++)
        if (g_sound_loaded[i]) UnloadSound(g_sounds[i]);
    if (g_pen_ready) { UnloadRenderTexture(g_pen); g_pen_ready = false; }
    if (g_audio_inited) CloseAudioDevice();
    CloseWindow();
}

// ---------- 角色 ----------
rt_sprite rt_sprite_load(const char* image_path) {
    if (g_sprite_count >= RT_MAX_SPRITES) return -1;
    int id = g_sprite_count++;
    RtSprite* s = &g_sprites[id];
    s->kind = RT_SPR_TEXTURE;
    s->tex = LoadTexture(image_path);
    s->loaded = (s->tex.id != 0);
    s->x = 0;
    s->y = 0;
    s->heading = 0;
    s->scale = 1.0f;
    s->bubble[0] = '\0';
    return id;
}

static bool sprite_valid(rt_sprite s);   // 定义在下方

// 造型纹理缓存：同名造型只加载一次（切换造型可以每帧调用）
#define RT_MAX_COSTUMES 64
static struct { char name[128]; Texture2D tex; } g_costumes[RT_MAX_COSTUMES];
static int g_costume_count = 0;

static Texture2D costume_tex(const char* path) {
    for (int i = 0; i < g_costume_count; i++)
        if (strncmp(g_costumes[i].name, path, sizeof(g_costumes[i].name)) == 0)
            return g_costumes[i].tex;
    Texture2D t = LoadTexture(path);
    if (g_costume_count < RT_MAX_COSTUMES) {
        strncpy(g_costumes[g_costume_count].name, path, sizeof(g_costumes[0].name) - 1);
        g_costumes[g_costume_count].name[sizeof(g_costumes[0].name) - 1] = '\0';
        g_costumes[g_costume_count].tex = t;
        g_costume_count++;
    }
    return t;
}

void rt_costume(rt_sprite s, const char* path) {
    if (!sprite_valid(s)) return;
    RtSprite* sp = &g_sprites[s];
    Texture2D t = costume_tex(path);
    if (t.id == 0) return;              // 加载失败：保持原造型
    sp->kind = RT_SPR_TEXTURE;
    sp->tex = t;
    sp->loaded = true;
}

rt_sprite rt_sprite_rect(float x, float y, float size) {
    if (g_sprite_count >= RT_MAX_SPRITES) return -1;
    int id = g_sprite_count++;
    RtSprite* s = &g_sprites[id];
    s->kind = RT_SPR_RECT;
    s->loaded = true;
    s->size = size;
    s->x = x;
    s->y = y;
    s->heading = 0;
    s->scale = 1.0f;
    s->bubble[0] = '\0';
    return id;
}

static bool sprite_valid(rt_sprite s) {
    return s >= 0 && s < g_sprite_count;
}

void rt_sprite_draw(rt_sprite s) {
    if (sprite_valid(s) && g_sprites[s].hidden) return;
    if (!sprite_valid(s) || !g_sprites[s].loaded) return;
    RtSprite* sp = &g_sprites[s];
    Vector2 pos = stage_to_screen(sp->x, sp->y);

    if (sp->kind == RT_SPR_RECT) {
        float sz = sp->size * sp->scale;
        Rectangle r = {pos.x - sz * 0.5f, pos.y - sz * 0.5f, sz, sz};
        DrawRectangleRec(r, MAROON);
        DrawRectangleLinesEx(r, 2, BLACK);
        if (sp->bubble[0] != '\0')
            DrawText(sp->bubble, (int)(pos.x + sz * 0.5f),
                     (int)(pos.y - sz * 0.5f - 20), 20, BLACK);
        return;
    }

    // 以纹理中心为锚点绘制。heading 是舞台系（y 向上）的逆时针角，
    // raylib 的 rotation 是屏幕系（y 向下）的顺时针角——取负号才能让
    // 贴图跟着运动方向转（90° 朝上时图也朝上）。
    float w = sp->tex.width * sp->scale;
    float h = sp->tex.height * sp->scale;
    Rectangle src = {0, 0, (float)sp->tex.width, (float)sp->tex.height};
    Rectangle dst = {pos.x, pos.y, w, h};
    Vector2 origin = {w * 0.5f, h * 0.5f};
    DrawTexturePro(sp->tex, src, dst, origin, -sp->heading, WHITE);

    if (sp->bubble[0] != '\0') {
        DrawText(sp->bubble, (int)(pos.x + w * 0.5f), (int)(pos.y - h * 0.5f - 20),
                 20, BLACK);
    }
}

void rt_move(rt_sprite s, float steps) {
    if (!sprite_valid(s)) return;
    // 朝向按数学约定：0° 朝右、90° 朝上（逆时针），与编辑器预览完全一致
    float rad = g_sprites[s].heading * (RT_PI / 180.0f);
    g_sprites[s].x += cosf(rad) * steps;
    g_sprites[s].y += sinf(rad) * steps;
}

void rt_goto(rt_sprite s, float x, float y) {
    if (!sprite_valid(s)) return;
    g_sprites[s].x = x;
    g_sprites[s].y = y;
}

void rt_turn(rt_sprite s, float degrees) {
    if (!sprite_valid(s)) return;
    g_sprites[s].heading += degrees;
}

void rt_point(rt_sprite s, float degrees) {
    if (!sprite_valid(s)) return;
    g_sprites[s].heading = degrees;
}

float rt_x(rt_sprite s) { return sprite_valid(s) ? g_sprites[s].x : 0.0f; }
float rt_y(rt_sprite s) { return sprite_valid(s) ? g_sprites[s].y : 0.0f; }

// 精灵的半宽/半高（纹理用图片尺寸，方块用边长；均乘缩放）
static void sprite_half(const RtSprite* sp, float* hw, float* hh) {
    if (sp->kind == RT_SPR_TEXTURE) {
        *hw = sp->tex.width  * sp->scale * 0.5f;
        *hh = sp->tex.height * sp->scale * 0.5f;
    } else {
        *hw = *hh = sp->size * sp->scale * 0.5f;
    }
}

bool rt_touching(rt_sprite a, rt_sprite b) {
    if (sprite_valid(a) && g_sprites[a].hidden) return false;
    if (sprite_valid(b) && g_sprites[b].hidden) return false;
    if (!sprite_valid(a) || !sprite_valid(b)) return false;
    const RtSprite* sa = &g_sprites[a];
    const RtSprite* sb = &g_sprites[b];
    float ahw, ahh, bhw, bhh;
    sprite_half(sa, &ahw, &ahh);
    sprite_half(sb, &bhw, &bhh);
    float dx = sa->x - sb->x; if (dx < 0) dx = -dx;
    float dy = sa->y - sb->y; if (dy < 0) dy = -dy;
    return dx < (ahw + bhw) && dy < (ahh + bhh);
}

// ---------- 输入 ----------
bool rt_key_down(int key) { return IsKeyDown(key); }
bool rt_key_pressed(int key) { return IsKeyPressed(key); }   // 本帧刚按下（边沿）
bool rt_mouse_clicked(void) { return IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

void rt_show(rt_sprite s) { if (sprite_valid(s)) g_sprites[s].hidden = false; }
void rt_hide(rt_sprite s) { if (sprite_valid(s)) g_sprites[s].hidden = true; }

// 碰到舞台边缘就反弹：镜像朝向并把精灵夹回舞台内（Scratch 的 if on edge, bounce）
void rt_bounce(rt_sprite s) {
    if (!sprite_valid(s)) return;
    RtSprite* sp = &g_sprites[s];
    float hw, hh;
    sprite_half(sp, &hw, &hh);
    float xmax = (float)g_stage_w * 0.5f - hw, ymax = (float)g_stage_h * 0.5f - hh;
    if (sp->x > xmax)  { sp->x = xmax;  sp->heading = 180.0f - sp->heading; }
    if (sp->x < -xmax) { sp->x = -xmax; sp->heading = 180.0f - sp->heading; }
    if (sp->y > ymax)  { sp->y = ymax;  sp->heading = -sp->heading; }
    if (sp->y < -ymax) { sp->y = -ymax; sp->heading = -sp->heading; }
}

// 精灵是否碰到鼠标指针（AABB 含点）
bool rt_touching_mouse(rt_sprite s) {
    if (!sprite_valid(s) || g_sprites[s].hidden) return false;
    RtSprite* sp = &g_sprites[s];
    float hw, hh;
    sprite_half(sp, &hw, &hh);
    float mx = rt_mouse_x(), my = rt_mouse_y();
    return mx >= sp->x - hw && mx <= sp->x + hw && my >= sp->y - hh && my <= sp->y + hh;
}

// 计时器：舞台启动起的秒数，可归零（Scratch 的 timer / reset timer）
static double g_timer_base = 0.0;
double rt_timer(void) { return GetTime() - g_timer_base; }
void rt_timer_reset(void) { g_timer_base = GetTime(); }
// 等待 x 秒：整个主循环停住（预览端同语义——全部精灵一起冻结）
void rt_wait(float secs) { if (secs > 0) WaitTime(secs); }
bool rt_mouse_down(int button) { return IsMouseButtonDown(button); }
float rt_mouse_x(void) {
    return (float)GetMouseX() - (float)g_stage_w * 0.5f;
}
float rt_mouse_y(void) {
    return (float)g_stage_h * 0.5f - (float)GetMouseY();
}

// ---------- 平台 / 工具 ----------
int rt_random(int lo, int hi) {
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    return GetRandomValue(lo, hi);
}
int rt_screen_w(void) { return g_stage_w; }
int rt_screen_h(void) { return g_stage_h; }
long long rt_frame_index(void) { return g_frame_index; }

// ---------- 画笔（持久层） ----------
void rt_pen_clear(void) {
    if (!g_pen_ready) return;
    BeginTextureMode(g_pen);
    ClearBackground(BLANK);
    EndTextureMode();
}
void rt_pen_color(int r, int g, int b) {
    g_pen_color = (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
}
void rt_pen_size(float w) { g_pen_size = w < 1.0f ? 1.0f : w; }
void rt_pen_line(float x1, float y1, float x2, float y2) {
    if (!g_pen_ready) return;
    Vector2 a = stage_to_screen(x1, y1), b = stage_to_screen(x2, y2);
    BeginTextureMode(g_pen);
    DrawLineEx(a, b, g_pen_size, g_pen_color);
    EndTextureMode();
}
void rt_pen_dot(float x, float y) {
    if (!g_pen_ready) return;
    Vector2 p = stage_to_screen(x, y);
    BeginTextureMode(g_pen);
    DrawCircleV(p, g_pen_size, g_pen_color);
    EndTextureMode();
}

// ---------- 外观 ----------
void rt_say(rt_sprite s, const char* text) {
    if (!sprite_valid(s)) return;
    if (text) {
        strncpy(g_sprites[s].bubble, text, sizeof(g_sprites[s].bubble) - 1);
        g_sprites[s].bubble[sizeof(g_sprites[s].bubble) - 1] = '\0';
    } else {
        g_sprites[s].bubble[0] = '\0';
    }
}

void rt_set_scale(rt_sprite s, float scale) {
    if (sprite_valid(s)) g_sprites[s].scale = scale;
}

void rt_draw_text(const char* text, float x, float y, int size) {
    Vector2 p = stage_to_screen(x, y);
    DrawText(text ? text : "", (int)p.x, (int)p.y, size, BLACK);
}

void rt_draw_int(long long n, float x, float y, int size) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", n);
    Vector2 p = stage_to_screen(x, y);
    DrawText(buf, (int)p.x, (int)p.y, size, BLACK);
}

// ---------- 声音 ----------
rt_sound rt_sound_load(const char* path) {
    if (g_sound_count >= RT_MAX_SOUNDS) return -1;
    int id = g_sound_count++;
    g_sounds[id] = LoadSound(path);
    g_sound_loaded[id] = true;
    return id;
}

void rt_sound_play(rt_sound snd) {
    if (snd >= 0 && snd < g_sound_count && g_sound_loaded[snd])
        PlaySound(g_sounds[snd]);
}

void rt_play_tone(int freq, int ms) {
    // 生成一段正弦波蜂鸣（无音频设备时静默）
    if (!g_audio_inited || freq <= 0 || ms <= 0) return;
    int rate = 22050;
    unsigned int n = (unsigned int)(rate * ms / 1000);
    if (n == 0 || n > 220500) return;
    short* data = (short*)RL_MALLOC(n * sizeof(short));
    if (!data) return;
    for (unsigned int i = 0; i < n; i++)
        data[i] = (short)(6000.0f * sinf(2.0f * RT_PI * freq * i / rate));
    Wave w = { n, (unsigned int)rate, 16, 1, data };
    Sound s = LoadSoundFromWave(w);
    PlaySound(s);
    UnloadSound(s);
    RL_FREE(data);
}

// ---------- 广播 / 事件 ----------
void rt_broadcast(const char* message) {
    if (!message || g_events_next_n >= RT_MAX_EVENTS) return;
    strncpy(g_events_next[g_events_next_n], message, RT_MSG_LEN - 1);
    g_events_next[g_events_next_n][RT_MSG_LEN - 1] = '\0';
    g_events_next_n++;
}

bool rt_received(const char* message) {
    if (!message) return false;
    for (int i = 0; i < g_events_cur_n; i++)
        if (strcmp(g_events_cur[i], message) == 0) return true;
    return false;
}
