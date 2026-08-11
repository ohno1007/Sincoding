// debug_overlay.cpp — 原生成品的 F12 调试面板（Dear ImGui + rlImGui）
//
// 只在 `--debug` 构建里参与链接；发布构建既不生成调试钩子调用，也不链接本文件，
// 因此**零开销**（见 tools/build_native.sh 的 --debug 分支）。
//
// 数据来源：sinc --debug 在生成的 C 里注入
//   sin_dbg_line(n)              —— 执行到源码第 n 行
//   sin_dbg_set_i/f/b/s(name, v) —— 标量变量的最新值
// 本文件把它们记录下来，并在每帧末尾用 imgui 画出监视面板。
//
// 面板能力：当前行 / 变量监视 / 帧号 / 暂停·逐帧 / 精灵检查器 / 日志。
#include "imgui.h"
#include "rlImGui.h"
#include "raylib.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// 说明：面板文案一律用 ASCII —— raylib 与 imgui 的**默认字体不含中文字形**，
// 用中文会渲染成 "????"。要显示中文需随成品打包一份 CJK 字体，暂不引入。

extern "C" {

// ---------------- 调试状态（由生成的 C 调用填充） ----------------
namespace {

struct DbgVar {
    std::string name;
    std::string value;   // 统一按文本存，面板只管展示
    const char* type;
};

long long g_line = 0;
long long g_frame = 0;
bool g_open = false;         // F12 开关（SIN_DBG_OPEN=1 可默认展开，便于无头验证）
bool g_paused = false;       // 暂停中
bool g_stepOnce = false;     // 逐帧：跑一帧再停
std::vector<DbgVar> g_vars;
std::vector<std::string> g_log;

DbgVar* findVar(const char* n) {
    for (auto& v : g_vars) if (v.name == n) return &v;
    g_vars.push_back({ n, "", "" });
    return &g_vars.back();
}

} // namespace

void sin_dbg_line(long long line) { g_line = line; }

void sin_dbg_set_i(const char* n, long long v) {
    DbgVar* d = findVar(n); d->value = std::to_string(v); d->type = "int";
}
void sin_dbg_set_f(const char* n, double v) {
    char buf[48]; snprintf(buf, sizeof(buf), "%g", v);
    DbgVar* d = findVar(n); d->value = buf; d->type = "float";
}
void sin_dbg_set_b(const char* n, bool v) {
    DbgVar* d = findVar(n); d->value = v ? "true" : "false"; d->type = "bool";
}
void sin_dbg_set_s(const char* n, const char* v) {
    DbgVar* d = findVar(n); d->value = std::string("\"") + (v ? v : "") + "\""; d->type = "string";
}

// ---------------- 面板生命周期（由 runtime 调用） ----------------
void sin_dbg_init(void) {
    rlImGuiSetup(true);
    const char* e = std::getenv("SIN_DBG_OPEN");
    if (e && e[0] == '1') g_open = true;
#ifdef __ANDROID__
    // 手机：没有 F12 也没有环境变量——默认展开面板，字体/控件放大适配高分屏
    g_open = true;
    ImGui::GetIO().FontGlobalScale = 2.0f;
    ImGui::GetStyle().ScaleAllSizes(2.0f);
#endif
}
void sin_dbg_shutdown(void) { rlImGuiShutdown(); }

// 是否应当阻塞当前帧（暂停中且未点「逐帧」）
bool sin_dbg_blocked(void) { return g_paused && !g_stepOnce; }
void sin_dbg_frame_done(void) { g_frame++; if (g_stepOnce) { g_stepOnce = false; g_paused = true; } }

// 每帧末（EndDrawing 之前）绘制面板
void sin_dbg_draw(int spriteCount, const float* sx, const float* sy) {
    if (IsKeyPressed(KEY_F12)) g_open = !g_open;
#ifdef __ANDROID__
    // 手机没有 F12：右上角 DBG 按钮开关（左下角留给虚拟手柄）。
    // 扫全部触点做边沿检测——按住手柄的同时也能用另一根手指点开面板。
    {
        Rectangle chip = { (float)GetScreenWidth() - 124.0f, 10.0f, 114.0f, 56.0f };
        static bool chipPrev = false;
        bool chipDown = false;
        for (int t = 0; t < GetTouchPointCount(); t++)
            if (CheckCollisionPointRec(GetTouchPosition(t), chip)) chipDown = true;
        if (chipDown && !chipPrev) g_open = !g_open;
        chipPrev = chipDown;
        DrawRectangleRounded(chip, 0.3f, 8, Fade(g_open ? DARKBLUE : BLACK, 0.38f));
        DrawText("DBG", (int)chip.x + 32, (int)chip.y + 16, 28, Fade(WHITE, 0.9f));
    }
#endif
    if (!g_open) {
#ifndef __ANDROID__
        // 收起时给个角标提示，免得用户不知道有这功能
        DrawText("F12: debug panel", 8, GetScreenHeight() - 22, 16, Fade(GRAY, 0.7f));
#endif
        return;
    }
    rlImGuiBegin();
#ifdef __ANDROID__
    ImGui::SetNextWindowSize(ImVec2(560, 680), ImGuiCond_FirstUseEver);   // 2x 缩放下手指可点
#else
    ImGui::SetNextWindowSize(ImVec2(320, 380), ImGuiCond_FirstUseEver);
#endif
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Sincoding Debugger (F12)")) {
        ImGui::Text("line: %lld    frame: %lld", g_line, g_frame);
        ImGui::Separator();

        if (ImGui::Button(g_paused ? "Resume" : "Pause")) g_paused = !g_paused;
        ImGui::SameLine();
        if (ImGui::Button("Step frame")) { g_stepOnce = true; g_paused = false; }
        ImGui::SameLine();
        ImGui::Text("%s", g_paused ? "paused" : "running");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (g_vars.empty()) ImGui::TextDisabled("(no scalar variables yet)");
            if (ImGui::BeginTable("vars", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                for (auto& v : g_vars) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(v.name.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(v.value.c_str());
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", v.type ? v.type : "");
                }
                ImGui::EndTable();
            }
        }
        if (ImGui::CollapsingHeader("Sprites", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < spriteCount; i++)
                ImGui::Text("#%d  x=%.1f  y=%.1f", i, sx ? sx[i] : 0.f, sy ? sy[i] : 0.f);
            if (spriteCount == 0) ImGui::TextDisabled("(none)");
        }
        if (ImGui::CollapsingHeader("Log")) {
            for (auto& l : g_log) ImGui::TextUnformatted(l.c_str());
            if (g_log.empty()) ImGui::TextDisabled("(none)");
        }
    }
    ImGui::End();
    rlImGuiEnd();
}

void sin_dbg_log(const char* msg) {
    if (!msg) return;
    g_log.push_back(msg);
    if (g_log.size() > 200) g_log.erase(g_log.begin());
}

} // extern "C"
