/* integration/demo_window.c — zcsr platform API 功能测试 demo.
 *
 * 验证所有 platform 窗口/显示器/DPI API:
 *   - zcsr_display_count / zcsr_display_get      (多显示器枚举)
 *   - zcsr_surface_set_mode                       (窗口/无边框/全屏)
 *   - zcsr_surface_set_client_size                (分辨率切换)
 *   - zcsr_surface_get_client_size                (读取当前大小)
 *   - zcsr_surface_get_dpi_scale                  (DPI 缩放)
 *   - zcsr_surface_move_to_display                (跨显示器移动)
 *   - zcsr_surface_set_size_limits                (最小/最大尺寸限制)
 *   - zcsr_surface_set_dpi_callback               (DPI 变化回调)
 *   - zcsr_surface_set_focus_callback             (焦点变化回调)
 *
 * 键盘控制:
 *   1 / 2 / 3   — 窗口化 / 无边框 / 全屏
 *   + / -       — 客户区 +100 / -100 像素
 *   Tab         — 移动到下一个显示器
 *   L           — 切换大小限制 (min 400x300, max 1200x900)
 *   R           — 重置为默认 800x600 窗口化
 *   ESC         — 退出
 *
 * 屏幕上实时显示状态; 控制台也输出事件日志.
 */
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

/* ---- 状态 ---- */
static zcsr_surface*   g_surface  = NULL;
static zcsr_gl_renderer* g_renderer = NULL;
static zcsr_input*     g_input    = NULL;

static zcsr_window_mode g_mode     = ZCSR_WINDOW_WINDOWED;
static int g_client_w = 800;
static int g_client_h = 600;
static int g_current_display = 0;
static bool g_limits_on = false;
static float g_dpi_scale = 1.0f;
static bool g_focused = true;

/* 事件日志 (环形, 最近 8 条) */
#define LOG_MAX 8
static char g_log[LOG_MAX][128];
static int  g_log_count = 0;
static int  g_log_head = 0;

static void log_event(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    strncpy(g_log[g_log_head], buf, sizeof(g_log[g_log_head]) - 1);
    g_log[g_log_head][sizeof(g_log[g_log_head]) - 1] = '\0';
    g_log_head = (g_log_head + 1) % LOG_MAX;
    if (g_log_count < LOG_MAX) g_log_count++;
    printf("[window-demo] %s\n", buf);
}

/* ---- 回调 ---- */
static void on_dpi_changed(zcsr_surface* s, float new_scale, void* user) {
    (void)s; (void)user;
    g_dpi_scale = new_scale;
    log_event("DPI callback: scale=%.2f", new_scale);
}

static void on_focus(zcsr_surface* s, zcsr_focus_event ev, void* user) {
    (void)s; (void)user;
    g_focused = (ev == ZCSR_FOCUS_GAINED);
    log_event("Focus: %s", g_focused ? "GAINED" : "LOST");
}

/* ---- 显示器信息 ---- */
static void print_displays(void) {
    int count = zcsr_display_count();
    printf("\n=== Displays (%d) ===\n", count);
    for (int i = 0; i < count; i++) {
        zcsr_display_info info;
        if (zcsr_display_get(i, &info)) {
            printf("  [%d] id=%d name=\"%s\" pos=(%d,%d) size=%dx%d "
                   "refresh=%dHz dpi=%.2f %s\n",
                   i, info.id, info.name, info.x, info.y,
                   info.width, info.height, info.refresh_hz,
                   info.dpi_scale, info.is_primary ? "(PRIMARY)" : "");
        }
    }
    printf("====================\n\n");
}

/* ---- 渲染 ---- */
static void render_frame(void) {
    zcsr_gl_begin(g_renderer);

    /* 背景: 深色 */
    zcsr_gl_fill_rect(g_renderer, 0.0f, 0.0f, (float)g_client_w, (float)g_client_h,
                      0.08f, 0.08f, 0.12f, 1.0f);

    float y = 20.0f;
    const float title_scale = 2.0f;
    const float text_scale  = 1.5f;
    const float dim_scale   = 1.2f;

    /* 标题 */
    zcsr_gl_draw_text(g_renderer, "zcsr Window API Test", 20, y,
                      title_scale, 0.9f, 0.9f, 1.0f, 1.0f);
    y += 40;

    /* 当前状态 */
    const char* mode_str = "UNKNOWN";
    switch (g_mode) {
        case ZCSR_WINDOW_WINDOWED:   mode_str = "WINDOWED";   break;
        case ZCSR_WINDOW_BORDERLESS: mode_str = "BORDERLESS"; break;
        case ZCSR_WINDOW_FULLSCREEN: mode_str = "FULLSCREEN"; break;
    }
    char line[256];
    snprintf(line, sizeof(line), "Mode: %s", mode_str);
    zcsr_gl_draw_text(g_renderer, line, 20, y, text_scale,
                      0.5f, 1.0f, 0.5f, 1.0f);
    y += 30;

    snprintf(line, sizeof(line), "Client: %dx%d", g_client_w, g_client_h);
    zcsr_gl_draw_text(g_renderer, line, 20, y, text_scale,
                      0.8f, 0.8f, 0.8f, 1.0f);
    y += 28;

    snprintf(line, sizeof(line), "DPI scale: %.2f  Focus: %s",
             g_dpi_scale, g_focused ? "yes" : "no");
    zcsr_gl_draw_text(g_renderer, line, 20, y, text_scale,
                      0.8f, 0.8f, 0.8f, 1.0f);
    y += 28;

    snprintf(line, sizeof(line), "Display: %d  Limits: %s",
             g_current_display, g_limits_on ? "ON (400x300 ~ 1200x900)" : "off");
    zcsr_gl_draw_text(g_renderer, line, 20, y, text_scale,
                      0.8f, 0.8f, 0.8f, 1.0f);
    y += 40;

    /* 控制说明 */
    zcsr_gl_draw_text(g_renderer, "Controls:", 20, y, text_scale,
                      1.0f, 0.9f, 0.5f, 1.0f);
    y += 28;
    zcsr_gl_draw_text(g_renderer, "1=Windowed  2=Borderless  3=Fullscreen",
                      30, y, dim_scale, 0.7f, 0.7f, 0.7f, 1.0f);
    y += 22;
    zcsr_gl_draw_text(g_renderer, "+/- = resize 100px  Tab=next display",
                      30, y, dim_scale, 0.7f, 0.7f, 0.7f, 1.0f);
    y += 22;
    zcsr_gl_draw_text(g_renderer, "L=toggle size limits  R=reset  ESC=quit",
                      30, y, dim_scale, 0.7f, 0.7f, 0.7f, 1.0f);
    y += 36;

    /* 显示器列表 */
    int disp_count = zcsr_display_count();
    snprintf(line, sizeof(line), "Displays (%d):", disp_count);
    zcsr_gl_draw_text(g_renderer, line, 20, y, text_scale,
                      1.0f, 0.9f, 0.5f, 1.0f);
    y += 28;
    for (int i = 0; i < disp_count && i < 4; i++) {
        zcsr_display_info info;
        if (zcsr_display_get(i, &info)) {
            snprintf(line, sizeof(line), "[%d] %s %dx%d @%dHz dpi=%.2f%s",
                     i, info.name, info.width, info.height,
                     info.refresh_hz, info.dpi_scale,
                     (i == g_current_display) ? "  <--" : "");
            zcsr_gl_draw_text(g_renderer, line, 30, y, dim_scale,
                              0.6f, 0.8f, 1.0f, 1.0f);
            y += 22;
        }
    }
    y += 10;

    /* 事件日志 */
    zcsr_gl_draw_text(g_renderer, "Event log:", 20, y, text_scale,
                      1.0f, 0.5f, 0.5f, 1.0f);
    y += 28;
    for (int i = 0; i < g_log_count; i++) {
        int idx = (g_log_head - g_log_count + i + LOG_MAX) % LOG_MAX;
        zcsr_gl_draw_text(g_renderer, g_log[idx], 30, y, dim_scale,
                          0.85f, 0.7f, 0.7f, 1.0f);
        y += 20;
    }

    zcsr_gl_flush(g_renderer);
    zcsr_gl_present(g_renderer);
}

/* ---- 主循环 ---- */
int main(void) {
    static unsigned char renderer_buf[8 * 1024 * 1024];
    static unsigned char input_buf[4 * 1024];

    printf("=== zcsr Window API Test Demo ===\n");

    /* 先枚举显示器 */
    print_displays();

    /* 创建窗口 */
    g_surface = zcsr_surface_create("zcsr window test",
                                    (zcsr_rect){ 100, 100, g_client_w, g_client_h });
    if (!g_surface) {
        printf("FAIL: zcsr_surface_create returned NULL (headless?)\n");
        return 1;
    }

    /* 渲染器 */
    g_renderer = zcsr_gl_create(g_surface, renderer_buf, sizeof renderer_buf);
    if (!g_renderer) {
        printf("FAIL: zcsr_gl_create returned NULL\n");
        zcsr_surface_destroy(g_surface);
        return 1;
    }
    zcsr_gl_set_vsync(g_renderer, true);

    /* 输入 */
    g_input = zcsr_input_init(input_buf, sizeof input_buf);
    if (!g_input) {
        printf("FAIL: zcsr_input_init returned NULL\n");
        zcsr_gl_destroy(g_renderer);
        zcsr_surface_destroy(g_surface);
        return 1;
    }

    /* 注册回调 */
    zcsr_surface_set_dpi_callback(g_surface, on_dpi_changed, NULL);
    zcsr_surface_set_focus_callback(g_surface, on_focus, NULL);

    /* 初始 DPI */
    g_dpi_scale = zcsr_surface_get_dpi_scale(g_surface);
    log_event("Init: client=%dx%d dpi=%.2f", g_client_w, g_client_h, g_dpi_scale);

    bool running = true;
    while (running) {
        /* NOTE: input_pump MUST run before surface_pump_events on Windows
         * (same fix as demo_2048 / demo_physics). */
        zcsr_input_pump(g_input, g_surface);
        zcsr_surface_pump_events(g_surface);

        /* 处理按键 */
        zcsr_key_event ev;
        while (zcsr_input_poll(g_input, &ev)) {
            if (ev.action != ZCSR_KEY_DOWN) continue;

            switch (ev.key) {
                case 27: /* ESC */
                    running = false;
                    log_event("ESC pressed, exiting");
                    break;

                case 49: /* '1' */
                    if (zcsr_surface_set_mode(g_surface, ZCSR_WINDOW_WINDOWED)) {
                        g_mode = ZCSR_WINDOW_WINDOWED;
                        log_event("set_mode: WINDOWED OK");
                    } else {
                        log_event("set_mode: WINDOWED FAILED");
                    }
                    break;

                case 50: /* '2' */
                    if (zcsr_surface_set_mode(g_surface, ZCSR_WINDOW_BORDERLESS)) {
                        g_mode = ZCSR_WINDOW_BORDERLESS;
                        log_event("set_mode: BORDERLESS OK");
                    } else {
                        log_event("set_mode: BORDERLESS FAILED");
                    }
                    break;

                case 51: /* '3' */
                    if (zcsr_surface_set_mode(g_surface, ZCSR_WINDOW_FULLSCREEN)) {
                        g_mode = ZCSR_WINDOW_FULLSCREEN;
                        log_event("set_mode: FULLSCREEN OK");
                    } else {
                        log_event("set_mode: FULLSCREEN FAILED");
                    }
                    break;

                case 187: /* '+' (VK_OEM_PLUS = 0xBB) */
                case 107: /* numpad '+' */
                    g_client_w += 100;
                    g_client_h += 75;
                    if (zcsr_surface_set_client_size(g_surface, g_client_w, g_client_h)) {
                        log_event("resize: %dx%d OK", g_client_w, g_client_h);
                    } else {
                        log_event("resize: %dx%d FAILED (fullscreen?)", g_client_w, g_client_h);
                        g_client_w -= 100;
                        g_client_h -= 75;
                    }
                    break;

                case 189: /* '-' (VK_OEM_MINUS = 0xBD) */
                case 109: /* numpad '-' */
                    g_client_w = (g_client_w > 200) ? g_client_w - 100 : 200;
                    g_client_h = (g_client_h > 150) ? g_client_h - 75 : 150;
                    if (zcsr_surface_set_client_size(g_surface, g_client_w, g_client_h)) {
                        log_event("resize: %dx%d OK", g_client_w, g_client_h);
                    } else {
                        log_event("resize: %dx%d FAILED", g_client_w, g_client_h);
                    }
                    break;

                case 9: /* Tab */
                    {
                        int count = zcsr_display_count();
                        int next = (g_current_display + 1) % count;
                        zcsr_display_info info;
                        if (zcsr_display_get(next, &info)) {
                            if (zcsr_surface_move_to_display(g_surface, info.id)) {
                                g_current_display = next;
                                log_event("move_to_display: [%d] %s OK", next, info.name);
                            } else {
                                log_event("move_to_display: [%d] FAILED", next);
                            }
                        }
                    }
                    break;

                case 76: /* 'L' */
                    g_limits_on = !g_limits_on;
                    if (g_limits_on) {
                        if (zcsr_surface_set_size_limits(g_surface, 400, 300, 1200, 900)) {
                            log_event("size_limits: ON (400x300 ~ 1200x900)");
                        } else {
                            log_event("size_limits: ON FAILED");
                            g_limits_on = false;
                        }
                    } else {
                        if (zcsr_surface_set_size_limits(g_surface, 0, 0, 0, 0)) {
                            log_event("size_limits: OFF");
                        } else {
                            log_event("size_limits: OFF FAILED");
                        }
                    }
                    break;

                case 82: /* 'R' */
                    g_client_w = 800;
                    g_client_h = 600;
                    zcsr_surface_set_mode(g_surface, ZCSR_WINDOW_WINDOWED);
                    g_mode = ZCSR_WINDOW_WINDOWED;
                    zcsr_surface_set_client_size(g_surface, g_client_w, g_client_h);
                    log_event("reset: 800x600 windowed");
                    break;

                default:
                    break;
            }
        }

        /* 每帧刷新实际 client size (用户可能拖拽窗口边缘) */
        int actual_w = 0, actual_h = 0;
        zcsr_surface_get_client_size(g_surface, &actual_w, &actual_h);
        if (actual_w > 0 && actual_h > 0) {
            if (actual_w != g_client_w || actual_h != g_client_h) {
                g_client_w = actual_w;
                g_client_h = actual_h;
            }
        }
        g_dpi_scale = zcsr_surface_get_dpi_scale(g_surface);

        render_frame();
        zcsr_sleep_ms(16);
    }

    /* 清理 */
    zcsr_surface_set_dpi_callback(g_surface, NULL, NULL);
    zcsr_surface_set_focus_callback(g_surface, NULL, NULL);
    zcsr_gl_destroy(g_renderer);
    zcsr_surface_destroy(g_surface);

    printf("\n=== Window API test complete ===\n");
    return 0;
}
