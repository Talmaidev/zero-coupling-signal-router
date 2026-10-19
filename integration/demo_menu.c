/* integration/demo_menu.c — 通用 menu UI 组件演示.
 *
 * 展示 menu_ui.h 的用法: 主菜单 + 选项子菜单 + 禁用项.
 * 键盘: Up/Down 或 W/S 导航, Enter/Space 确认, ESC 返回/退出.
 */
#include "menu_ui.h"
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

#define WIN_W 640
#define WIN_H 480

/* 菜单 ID */
enum {
    MENU_START = 1,
    MENU_OPTIONS,
    MENU_ABOUT,
    MENU_QUIT,
    OPT_SOUND,
    OPT_FULLSCREEN,
    OPT_BACK,
};

/* 全局状态 */
static zcsr_surface*      g_surface  = NULL;
static zcsr_gl_renderer*  g_renderer = NULL;
static zcsr_input*        g_input    = NULL;

/* 菜单内存 (静态, 无堆分配) */
static unsigned char g_menu_buf[2048];
static zcsr_menu* g_main_menu = NULL;
static zcsr_menu* g_options_menu = NULL;

/* 当前显示的菜单 */
static zcsr_menu* g_current_menu = NULL;

/* 选项状态 */
static bool g_sound_on = true;
static bool g_fullscreen = false;

/* 动态更新的菜单项文字 (因为选项需要显示 ON/OFF) */
static char g_opt_sound_label[32];
static char g_opt_fullscreen_label[32];

static zcsr_menu_item g_main_items[] = {
    { "Start Game",  MENU_START,   false },
    { "Options",     MENU_OPTIONS, false },
    { "About",       MENU_ABOUT,   false },
    { "Coming Soon", 99,           true  }, /* 禁用项演示 */
    { "Quit",        MENU_QUIT,    false },
};

static zcsr_menu_item g_options_items[] = {
    { g_opt_sound_label,      OPT_SOUND,      false },
    { g_opt_fullscreen_label, OPT_FULLSCREEN, false },
    { "Back",                 OPT_BACK,       false },
};

static void refresh_option_labels(void) {
    snprintf(g_opt_sound_label, sizeof(g_opt_sound_label),
             "Sound: %s", g_sound_on ? "ON" : "OFF");
    snprintf(g_opt_fullscreen_label, sizeof(g_opt_fullscreen_label),
             "Fullscreen: %s", g_fullscreen ? "ON" : "OFF");
}

static void init_menus(void) {
    refresh_option_labels();

    g_main_menu = zcsr_menu_create(g_menu_buf, sizeof g_menu_buf,
        &(zcsr_menu_config){
            .title       = "zcsr MENU DEMO",
            .items       = g_main_items,
            .item_count  = 5,
            .x           = 180,
            .y           = 80,
            .item_height = 44,
            .text_scale  = 2.0f,
        });

    /* 选项菜单用同一块缓冲区的后半部分? 不行, 会覆盖.
     * 实际上我们用静态缓冲区分别存储. 这里简化: 选项菜单用另一个缓冲区. */
    static unsigned char opts_buf[2048];
    g_options_menu = zcsr_menu_create(opts_buf, sizeof opts_buf,
        &(zcsr_menu_config){
            .title       = "OPTIONS",
            .items       = g_options_items,
            .item_count  = 3,
            .x           = 180,
            .y           = 120,
            .item_height = 44,
            .text_scale  = 2.0f,
            .sel_bg_r    = 0.6f, .sel_bg_g = 0.3f, .sel_bg_b = 0.5f,
        });

    g_current_menu = g_main_menu;
}

static void render_frame(void) {
    zcsr_gl_begin(g_renderer);

    /* 背景 */
    zcsr_gl_fill_rect(g_renderer, 0.0f, 0.0f, (float)WIN_W, (float)WIN_H,
                      0.08f, 0.08f, 0.12f, 1.0f);

    /* 渲染当前菜单 */
    if (g_current_menu) {
        zcsr_menu_render(g_current_menu, g_renderer);
    }

    /* 底部提示 */
    zcsr_gl_draw_text(g_renderer,
        "Up/Down or W/S: navigate | Enter/Space: select | ESC: back/quit",
        40.0f, (float)(WIN_H - 30), 1.0f, 0.5f, 0.5f, 0.5f, 1.0f);

    zcsr_gl_flush(g_renderer);
    zcsr_gl_present(g_renderer);
}

static void handle_menu_result(int result) {
    if (result < 0) return; /* 普通导航 */

    if (g_current_menu == g_main_menu) {
        switch (result) {
            case MENU_START:
                printf("[menu] Start Game selected!\n");
                /* 这里可以切换到游戏状态 */
                break;
            case MENU_OPTIONS:
                printf("[menu] -> Options\n");
                refresh_option_labels();
                zcsr_menu_reset(g_options_menu);
                g_current_menu = g_options_menu;
                break;
            case MENU_ABOUT:
                printf("[menu] About: zcsr zero-coupling signal router\n");
                break;
            case MENU_QUIT:
                printf("[menu] Quit selected, exiting...\n");
                /* 通知主循环退出 */
                break;
        }
    } else if (g_current_menu == g_options_menu) {
        switch (result) {
            case OPT_SOUND:
                g_sound_on = !g_sound_on;
                refresh_option_labels();
                printf("[menu] Sound -> %s\n", g_sound_on ? "ON" : "OFF");
                break;
            case OPT_FULLSCREEN:
                g_fullscreen = !g_fullscreen;
                refresh_option_labels();
                printf("[menu] Fullscreen -> %s\n", g_fullscreen ? "ON" : "OFF");
                break;
            case OPT_BACK:
                printf("[menu] <- Back to main menu\n");
                g_current_menu = g_main_menu;
                break;
        }
    }
}

int main(void) {
    static unsigned char renderer_buf[8 * 1024 * 1024];
    static unsigned char input_buf[4 * 1024];

    printf("=== zcsr Menu UI Demo ===\n");

    g_surface = zcsr_surface_create("zcsr menu demo",
                                    (zcsr_rect){ 100, 100, WIN_W, WIN_H });
    if (!g_surface) { printf("FAIL: no surface\n"); return 1; }

    g_renderer = zcsr_gl_create(g_surface, renderer_buf, sizeof renderer_buf);
    if (!g_renderer) { printf("FAIL: no renderer\n"); zcsr_surface_destroy(g_surface); return 1; }
    zcsr_gl_set_vsync(g_renderer, true);

    g_input = zcsr_input_init(input_buf, sizeof input_buf);
    if (!g_input) { printf("FAIL: no input\n"); zcsr_gl_destroy(g_renderer); zcsr_surface_destroy(g_surface); return 1; }

    init_menus();

    bool running = true;
    bool quit_requested = false;

    while (running) {
        /* input_pump 必须在 surface_pump_events 之前 (Windows) */
        zcsr_input_pump(g_input, g_surface);
        zcsr_surface_pump_events(g_surface);

        zcsr_key_event ev;
        while (zcsr_input_poll(g_input, &ev)) {
            if (ev.action != ZCSR_KEY_DOWN) continue;

            int result = zcsr_menu_handle_key(g_current_menu, ev.key, ev.action);

            if (result == -2) {
                /* ESC */
                if (g_current_menu == g_options_menu) {
                    g_current_menu = g_main_menu;
                    printf("[menu] ESC -> back to main\n");
                } else {
                    printf("[menu] ESC -> quit\n");
                    running = false;
                }
            } else if (result >= 0) {
                handle_menu_result(result);
                if (result == MENU_QUIT && g_current_menu == g_main_menu) {
                    quit_requested = true;
                }
            }
        }

        if (quit_requested) running = false;

        render_frame();
        zcsr_sleep_ms(16);
    }

    zcsr_menu_destroy(g_main_menu);
    zcsr_menu_destroy(g_options_menu);
    zcsr_gl_destroy(g_renderer);
    zcsr_surface_destroy(g_surface);

    printf("=== Menu demo ended ===\n");
    return 0;
}
