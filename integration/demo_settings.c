/* integration/demo_settings.c — 游戏设置面板演示.
 *
 * 展示 settings_ui.h 用法: 显示设置 + 图形设置, 应用时实际调用 platform API.
 *
 * 键盘:
 *   Up/Down 或 W/S  — 切换设置项
 *   Left/Right 或 A/D — 修改值
 *   Tab            — 跳到底部按钮 (Apply/Cancel/Reset)
 *   Enter          — 应用
 *   ESC            — 取消并退出
 */
#include "settings_ui.h"
#include "zcsr/gl_render.h"
#include "zcsr/platform.h"
#include "zcsr/input.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
static void zcsr_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#  include <unistd.h>
static void zcsr_sleep_ms(int ms) { usleep((useconds_t)(ms * 1000)); }
#endif

#define WIN_W 720
#define WIN_H 640

/* ---- 设置项索引 ---- */
enum {
    SET_DISPLAY = 0,
    SET_MODE,
    SET_RESOLUTION,
    SET_REFRESH,
    SET_VSYNC,
    SET_FRAMELIMIT,
    SET_GFX_SEP,     /* separator */
    SET_QUALITY,
    SET_TEXTURE,
    SET_SHADOW,
    SET_AA,
    SET_SHARPNESS,
    SET_COUNT,
};

/* ---- 下拉选项 ---- */
static const char* mode_opts[]     = { "Windowed", "Borderless", "Fullscreen" };
static const char* res_opts[]      = { "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160" };
static const char* refresh_opts[]  = { "60Hz", "75Hz", "120Hz", "144Hz", "165Hz", "240Hz" };
static const char* vsync_opts[]    = { "Off", "On" };
static const char* fps_opts[]      = { "30", "60", "120", "144", "Unlimited" };
static const char* quality_opts[]  = { "Low", "Medium", "High", "Ultra" };
static const char* tex_opts[]      = { "Low", "Medium", "High" };
static const char* shadow_opts[]   = { "Off", "Low", "Medium", "High" };
static const char* aa_opts[]       = { "Off", "FXAA", "TAA", "MSAA 2x", "MSAA 4x" };

/* 显示器名称 (运行时填充) */
static char g_display_names[8][64];
static const char* g_display_opts[8];
static int g_display_count = 0;

/* ---- 全局状态 ---- */
static zcsr_surface*      g_surface  = NULL;
static zcsr_gl_renderer*  g_renderer = NULL;
static zcsr_input*        g_input    = NULL;
static zcsr_settings*     g_settings = NULL;

static zcsr_setting_item g_items[SET_COUNT];

static int g_res_w = 1280, g_res_h = 720;

/* ---- 初始化显示器列表 ---- */
static void init_display_list(void) {
    g_display_count = zcsr_display_count();
    if (g_display_count > 8) g_display_count = 8;
    for (int i = 0; i < g_display_count; i++) {
        zcsr_display_info info;
        if (zcsr_display_get(i, &info)) {
            snprintf(g_display_names[i], sizeof(g_display_names[i]),
                     "%s (%dx%d)", info.name, info.width, info.height);
        } else {
            snprintf(g_display_names[i], sizeof(g_display_names[i]), "Display %d", i);
        }
        g_display_opts[i] = g_display_names[i];
    }
}

/* ---- 应用设置到实际窗口 ---- */
static void apply_settings(void) {
    if (!g_settings) return;
    /* g_items 是调用者拥有的数组, apply 后 pending 值已写入 value/slider_value */
    zcsr_setting_item* items = g_items;

    /* 显示模式 */
    int mode = items[SET_MODE].value;
    zcsr_window_mode wm = ZCSR_WINDOW_WINDOWED;
    if (mode == 1) wm = ZCSR_WINDOW_BORDERLESS;
    else if (mode == 2) wm = ZCSR_WINDOW_FULLSCREEN;
    zcsr_surface_set_mode(g_surface, wm);
    printf("[settings] display mode: %s\n", mode_opts[mode]);

    /* 分辨率 (仅窗口模式下生效) */
    if (mode == 0) {
        static const int res_w[] = { 1280, 1600, 1920, 2560, 3840 };
        static const int res_h[] = { 720, 900, 1080, 1440, 2160 };
        int ri = items[SET_RESOLUTION].value;
        g_res_w = res_w[ri]; g_res_h = res_h[ri];
        zcsr_surface_set_client_size(g_surface, g_res_w, g_res_h);
        printf("[settings] resolution: %dx%d\n", g_res_w, g_res_h);
    }

    /* 显示器 */
    int di = items[SET_DISPLAY].value;
    if (di < g_display_count) {
        zcsr_display_info info;
        if (zcsr_display_get(di, &info)) {
            zcsr_surface_move_to_display(g_surface, info.id);
            printf("[settings] display: %s\n", info.name);
        }
    }

    /* 垂直同步 */
    zcsr_gl_set_vsync(g_renderer, items[SET_VSYNC].value == 1);
    printf("[settings] vsync: %s\n", vsync_opts[items[SET_VSYNC].value]);

    /* 帧率上限 (仅打印, 实际需在主循环控制) */
    printf("[settings] frame limit: %s\n", fps_opts[items[SET_FRAMELIMIT].value]);

    /* 图形选项 (仅打印, 渲染管线未实现这些效果) */
    printf("[settings] quality: %s | texture: %s | shadow: %s | AA: %s | sharpness: %.0f\n",
           quality_opts[items[SET_QUALITY].value],
           tex_opts[items[SET_TEXTURE].value],
           shadow_opts[items[SET_SHADOW].value],
           aa_opts[items[SET_AA].value],
           items[SET_SHARPNESS].slider_value);
}

/* ---- 重置为默认 ---- */
static void reset_defaults(void) {
    g_items[SET_DISPLAY].value = 0;
    g_items[SET_MODE].value = 0;
    g_items[SET_RESOLUTION].value = 2; /* 1080p */
    g_items[SET_REFRESH].value = 0;
    g_items[SET_VSYNC].value = 1; /* On */
    g_items[SET_FRAMELIMIT].value = 4; /* Unlimited */
    g_items[SET_QUALITY].value = 2; /* High */
    g_items[SET_TEXTURE].value = 2;
    g_items[SET_SHADOW].value = 2;
    g_items[SET_AA].value = 1; /* FXAA */
    g_items[SET_SHARPNESS].slider_value = 50.0f;
    zcsr_settings_reset(g_settings);
}

int main(void) {
    static unsigned char renderer_buf[8 * 1024 * 1024];
    static unsigned char input_buf[4 * 1024];
    static unsigned char settings_buf[4096];

    printf("=== zcsr Settings Demo ===\n");

    init_display_list();

    g_surface = zcsr_surface_create("zcsr settings",
                                    (zcsr_rect){ 100, 100, WIN_W, WIN_H });
    if (!g_surface) { printf("FAIL: no surface\n"); return 1; }

    g_renderer = zcsr_gl_create(g_surface, renderer_buf, sizeof renderer_buf);
    if (!g_renderer) { printf("FAIL: no renderer\n"); zcsr_surface_destroy(g_surface); return 1; }
    zcsr_gl_set_vsync(g_renderer, true);

    g_input = zcsr_input_init(input_buf, sizeof input_buf);
    if (!g_input) { printf("FAIL: no input\n"); zcsr_gl_destroy(g_renderer); zcsr_surface_destroy(g_surface); return 1; }

    /* 配置设置项 */
    memset(g_items, 0, sizeof(g_items));

    g_items[SET_DISPLAY] = (zcsr_setting_item){
        .label = "Display", .type = ZCSR_CTRL_DROPDOWN,
        .options = g_display_opts, .option_count = g_display_count, .value = 0 };
    g_items[SET_MODE] = (zcsr_setting_item){
        .label = "Display Mode", .type = ZCSR_CTRL_DROPDOWN,
        .options = mode_opts, .option_count = 3, .value = 0 };
    g_items[SET_RESOLUTION] = (zcsr_setting_item){
        .label = "Resolution", .type = ZCSR_CTRL_DROPDOWN,
        .options = res_opts, .option_count = 5, .value = 2 };
    g_items[SET_REFRESH] = (zcsr_setting_item){
        .label = "Refresh Rate", .type = ZCSR_CTRL_DROPDOWN,
        .options = refresh_opts, .option_count = 6, .value = 0 };
    g_items[SET_VSYNC] = (zcsr_setting_item){
        .label = "V-Sync", .type = ZCSR_CTRL_DROPDOWN,
        .options = vsync_opts, .option_count = 2, .value = 1 };
    g_items[SET_FRAMELIMIT] = (zcsr_setting_item){
        .label = "Frame Rate Limit", .type = ZCSR_CTRL_DROPDOWN,
        .options = fps_opts, .option_count = 5, .value = 4 };
    g_items[SET_GFX_SEP] = (zcsr_setting_item){
        .label = "--- Graphics ---", .type = ZCSR_CTRL_SEPARATOR };
    g_items[SET_QUALITY] = (zcsr_setting_item){
        .label = "Quality Preset", .type = ZCSR_CTRL_DROPDOWN,
        .options = quality_opts, .option_count = 4, .value = 2 };
    g_items[SET_TEXTURE] = (zcsr_setting_item){
        .label = "Texture Quality", .type = ZCSR_CTRL_DROPDOWN,
        .options = tex_opts, .option_count = 3, .value = 2 };
    g_items[SET_SHADOW] = (zcsr_setting_item){
        .label = "Shadow Quality", .type = ZCSR_CTRL_DROPDOWN,
        .options = shadow_opts, .option_count = 4, .value = 2 };
    g_items[SET_AA] = (zcsr_setting_item){
        .label = "Anti-Aliasing", .type = ZCSR_CTRL_DROPDOWN,
        .options = aa_opts, .option_count = 5, .value = 1 };
    g_items[SET_SHARPNESS] = (zcsr_setting_item){
        .label = "Sharpness", .type = ZCSR_CTRL_SLIDER,
        .min = 0.0f, .max = 100.0f, .step = 5.0f, .slider_value = 50.0f };

    g_settings = zcsr_settings_create(settings_buf, sizeof settings_buf,
        &(zcsr_settings_config){
            .title       = "VIDEO SETTINGS",
            .items       = g_items,
            .count       = SET_COUNT,
            .x           = 60,
            .y           = 30,
            .width       = 580,
            .item_height = 34,
            .text_scale  = 1.5f,
        });
    if (!g_settings) { printf("FAIL: settings create\n"); return 1; }

    printf("Ready. Up/Down navigate, Left/Right change, Tab=buttons, Enter=Apply, ESC=quit\n");

    bool running = true;
    while (running) {
        zcsr_input_pump(g_input, g_surface);
        zcsr_surface_pump_events(g_surface);

        zcsr_key_event ev;
        while (zcsr_input_poll(g_input, &ev)) {
            if (ev.action != ZCSR_KEY_DOWN) continue;
            int result = zcsr_settings_handle_key(g_settings, ev.key, ev.action);
            if (result == 1) {
                apply_settings();
            } else if (result == -1) {
                printf("[settings] cancelled / quit\n");
                running = false;
            }
        }

        /* 渲染 */
        zcsr_gl_begin(g_renderer);
        zcsr_gl_fill_rect(g_renderer, 0.0f, 0.0f, (float)WIN_W, (float)WIN_H,
                          0.05f, 0.06f, 0.09f, 1.0f);
        zcsr_settings_render(g_settings, g_renderer);

        /* 底部提示 */
        zcsr_gl_draw_text(g_renderer,
            "Up/Down: navigate | Left/Right: change | Tab: buttons | Enter: Apply | ESC: quit",
            40.0f, (float)(WIN_H - 24), 1.0f, 0.4f, 0.4f, 0.5f, 1.0f);

        zcsr_gl_flush(g_renderer);
        zcsr_gl_present(g_renderer);
        zcsr_sleep_ms(16);
    }

    zcsr_settings_destroy(g_settings);
    zcsr_gl_destroy(g_renderer);
    zcsr_surface_destroy(g_surface);
    printf("=== Settings demo ended ===\n");
    return 0;
}
