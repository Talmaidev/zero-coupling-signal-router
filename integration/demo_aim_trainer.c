/* ============================================================================
 * zcsr Aim Trainer — FPS 瞄准训练工具
 *
 * 玩法：
 *   - 全屏 borderless，黑色背景
 *   - 十字准星跟随鼠标
 *   - 红色目标随机出现，点击命中计分
 *   - 60 秒倒计时，结束显示统计
 *   - R 重新开始，ESC 退出
 *
 * 统计：命中数、未命中数、命中率、平均反应时间、最佳反应时间
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zcsr/platform.h"
#include "zcsr/gl_render.h"
#include "zcsr/input.h"
#include "zcsr/input_ext.h"
#include "zcsr/clock.h"

/* -------------------------------------------------------------------------- */
/*  Constants                                                                 */
/* -------------------------------------------------------------------------- */

#define GAME_DURATION 60.0  /* seconds */
#define TARGET_SIZE   40    /* pixels */
#define CROSSHAIR_LEN 12
#define CROSSHAIR_GAP 4
#define MAX_SAMPLES   1024

/* Windows VK codes (zcsr keycode = native VK passthrough) */
#define VK_ESC   0x1B
#define VK_ENTER 0x0D
#define VK_R     0x52

/* -------------------------------------------------------------------------- */
/*  Global state                                                              */
/* -------------------------------------------------------------------------- */

static zcsr_surface*     g_surface  = NULL;
static zcsr_gl_renderer* g_renderer = NULL;
static zcsr_input*       g_input    = NULL;
static zcsr_input_ext*   g_inputext = NULL;

static int g_win_w = 1920;
static int g_win_h = 1080;

/* game state */
typedef enum {
    STATE_MENU,
    STATE_PLAYING,
    STATE_RESULT
} game_state;

static game_state g_state = STATE_MENU;
static double g_time_left = GAME_DURATION;
static int    g_hits = 0;
static int    g_misses = 0;
static double g_reaction_times[MAX_SAMPLES];
static int    g_sample_count = 0;
static double g_best_reaction = 9999.0;

/* target */
static int    g_target_x = 0;
static int    g_target_y = 0;
static double g_target_spawn_time = 0.0;
static int    g_target_visible = 0;

/* mouse */
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static uint8_t g_prev_buttons = 0;

/* timing */
static uint64_t g_prev_ns = 0;

/* -------------------------------------------------------------------------- */
/*  Utility                                                                   */
/* -------------------------------------------------------------------------- */

static double now_sec(void) {
    return (double)zcsr_clock_now_ns() / 1e9;
}

static void spawn_target(void) {
    int margin = TARGET_SIZE + 20;
    g_target_x = margin + rand() % (g_win_w - margin * 2);
    g_target_y = margin + rand() % (g_win_h - margin * 2);
    g_target_spawn_time = now_sec();
    g_target_visible = 1;
}

static int point_in_target(int px, int py) {
    int half = TARGET_SIZE / 2;
    return (px >= g_target_x - half && px <= g_target_x + half &&
            py >= g_target_y - half && py <= g_target_y + half);
}

static void reset_game(void) {
    g_time_left = GAME_DURATION;
    g_hits = 0;
    g_misses = 0;
    g_sample_count = 0;
    g_best_reaction = 9999.0;
    g_target_visible = 0;
    g_state = STATE_PLAYING;
    spawn_target();
}

/* -------------------------------------------------------------------------- */
/*  Rendering                                                                 */
/* -------------------------------------------------------------------------- */

static void draw_crosshair(int x, int y) {
    zcsr_gl_fill_rect(g_renderer,
        (float)(x - CROSSHAIR_LEN), (float)(y - 1),
        (float)(CROSSHAIR_LEN - CROSSHAIR_GAP), 2.0f,
        0.0f, 1.0f, 0.0f, 1.0f);
    zcsr_gl_fill_rect(g_renderer,
        (float)(x + CROSSHAIR_GAP), (float)(y - 1),
        (float)(CROSSHAIR_LEN - CROSSHAIR_GAP), 2.0f,
        0.0f, 1.0f, 0.0f, 1.0f);
    zcsr_gl_fill_rect(g_renderer,
        (float)(x - 1), (float)(y - CROSSHAIR_LEN),
        2.0f, (float)(CROSSHAIR_LEN - CROSSHAIR_GAP),
        0.0f, 1.0f, 0.0f, 1.0f);
    zcsr_gl_fill_rect(g_renderer,
        (float)(x - 1), (float)(y + CROSSHAIR_GAP),
        2.0f, (float)(CROSSHAIR_LEN - CROSSHAIR_GAP),
        0.0f, 1.0f, 0.0f, 1.0f);
    zcsr_gl_fill_rect(g_renderer,
        (float)(x - 1), (float)(y - 1), 2.0f, 2.0f,
        1.0f, 0.0f, 0.0f, 1.0f);
}

static void draw_target(int x, int y) {
    int half = TARGET_SIZE / 2;
    zcsr_gl_fill_rect(g_renderer,
        (float)(x - half), (float)(y - half),
        (float)TARGET_SIZE, (float)TARGET_SIZE,
        0.9f, 0.15f, 0.15f, 1.0f);
    zcsr_gl_fill_rect(g_renderer,
        (float)(x - half / 2), (float)(y - half / 2),
        (float)(TARGET_SIZE / 2), (float)(TARGET_SIZE / 2),
        1.0f, 1.0f, 1.0f, 1.0f);
    zcsr_gl_fill_rect(g_renderer,
        (float)(x - 3), (float)(y - 3), 6.0f, 6.0f,
        0.9f, 0.15f, 0.15f, 1.0f);
}

static void draw_text_centered(const char* text, int cx, int y, float scale,
                                float r, float g, float b) {
    int w = 0;
    const char* p = text;
    while (*p++) w++;
    int px = cx - (w * 8 * (int)scale) / 2;
    zcsr_gl_draw_text(g_renderer, text, (float)px, (float)y, scale, r, g, b, 1.0f);
}

static void render_menu(void) {
    draw_text_centered("ZCSR AIM TRAINER", g_win_w / 2, g_win_h / 2 - 80, 3.0f, 0.2f, 0.8f, 1.0f);
    draw_text_centered("Click the red targets as fast as you can", g_win_w / 2, g_win_h / 2 - 20, 1.5f, 0.7f, 0.7f, 0.7f);
    draw_text_centered("60 second mode", g_win_w / 2, g_win_h / 2 + 20, 1.5f, 0.7f, 0.7f, 0.7f);
    draw_text_centered("PRESS CLICK OR ENTER TO START", g_win_w / 2, g_win_h / 2 + 80, 2.0f, 1.0f, 1.0f, 0.3f);
    draw_text_centered("ESC to quit", g_win_w / 2, g_win_h / 2 + 130, 1.2f, 0.5f, 0.5f, 0.5f);
    draw_crosshair(g_mouse_x, g_mouse_y);
}

static void render_playing(void) {
    if (g_target_visible) {
        draw_target(g_target_x, g_target_y);
    }

    char buf[128];
    snprintf(buf, sizeof buf, "TIME: %.0f", g_time_left);
    zcsr_gl_draw_text(g_renderer, buf, 20.0f, 20.0f, 2.0f, 1.0f, 1.0f, 1.0f, 1.0f);

    snprintf(buf, sizeof buf, "HITS: %d", g_hits);
    zcsr_gl_draw_text(g_renderer, buf, 20.0f, 55.0f, 1.5f, 0.3f, 1.0f, 0.3f, 1.0f);

    snprintf(buf, sizeof buf, "MISS: %d", g_misses);
    zcsr_gl_draw_text(g_renderer, buf, 20.0f, 85.0f, 1.5f, 1.0f, 0.3f, 0.3f, 1.0f);

    int total = g_hits + g_misses;
    float acc = (total > 0) ? (float)g_hits / (float)total * 100.0f : 0.0f;
    snprintf(buf, sizeof buf, "ACC: %.1f%%", acc);
    zcsr_gl_draw_text(g_renderer, buf, 20.0f, 115.0f, 1.5f, 1.0f, 0.9f, 0.2f, 1.0f);

    draw_crosshair(g_mouse_x, g_mouse_y);
}

static void render_result(void) {
    int total = g_hits + g_misses;
    float acc = (total > 0) ? (float)g_hits / (float)total * 100.0f : 0.0f;

    double avg_reaction = 0.0;
    if (g_sample_count > 0) {
        double sum = 0.0;
        for (int i = 0; i < g_sample_count; i++) sum += g_reaction_times[i];
        avg_reaction = sum / g_sample_count;
    }

    draw_text_centered("RESULTS", g_win_w / 2, g_win_h / 2 - 160, 3.0f, 0.2f, 0.8f, 1.0f);

    char buf[128];
    snprintf(buf, sizeof buf, "Hits: %d", g_hits);
    draw_text_centered(buf, g_win_w / 2, g_win_h / 2 - 80, 2.0f, 0.3f, 1.0f, 0.3f);

    snprintf(buf, sizeof buf, "Misses: %d", g_misses);
    draw_text_centered(buf, g_win_w / 2, g_win_h / 2 - 40, 2.0f, 1.0f, 0.3f, 0.3f);

    snprintf(buf, sizeof buf, "Accuracy: %.1f%%", acc);
    draw_text_centered(buf, g_win_w / 2, g_win_h / 2, 2.0f, 1.0f, 0.9f, 0.2f);

    snprintf(buf, sizeof buf, "Avg Reaction: %.0f ms", avg_reaction * 1000.0);
    draw_text_centered(buf, g_win_w / 2, g_win_h / 2 + 40, 2.0f, 0.4f, 0.7f, 1.0f);

    if (g_best_reaction < 9999.0) {
        snprintf(buf, sizeof buf, "Best Reaction: %.0f ms", g_best_reaction * 1000.0);
        draw_text_centered(buf, g_win_w / 2, g_win_h / 2 + 80, 2.0f, 1.0f, 0.5f, 0.2f);
    }

    draw_text_centered("PRESS R TO RESTART", g_win_w / 2, g_win_h / 2 + 150, 1.8f, 1.0f, 1.0f, 0.3f);
    draw_text_centered("ESC to quit", g_win_w / 2, g_win_h / 2 + 190, 1.2f, 0.5f, 0.5f, 0.5f);

    draw_crosshair(g_mouse_x, g_mouse_y);
}

/* -------------------------------------------------------------------------- */
/*  Main                                                                      */
/* -------------------------------------------------------------------------- */

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    printf("zcsr aim trainer\n");

    /* seed random */
    srand((unsigned int)zcsr_clock_now_ns());
    g_prev_ns = zcsr_clock_now_ns();

    /* create surface (borderless fullscreen) */
    g_surface = zcsr_surface_create("zcsr aim trainer",
                                    (zcsr_rect){ 0, 0, 1280, 720 });
    if (!g_surface) { printf("FAIL: surface\n"); return 1; }

    /* get actual window size */
    zcsr_surface_get_client_size(g_surface, &g_win_w, &g_win_h);
    printf("window: %dx%d\n", g_win_w, g_win_h);

    /* create renderer */
    static unsigned char renderer_buf[8 * 1024 * 1024];
    g_renderer = zcsr_gl_create(g_surface, renderer_buf, sizeof renderer_buf);
    if (!g_renderer) { printf("FAIL: renderer\n"); zcsr_surface_destroy(g_surface); return 1; }
    zcsr_gl_set_vsync(g_renderer, true);

    /* create input */
    static unsigned char input_buf[4 * 1024];
    g_input = zcsr_input_init(input_buf, sizeof input_buf);
    if (!g_input) { printf("FAIL: input\n"); goto cleanup; }

    /* create inputext */
    static unsigned char inputext_buf[8 * 1024];
    g_inputext = zcsr_input_ext_init(inputext_buf, sizeof inputext_buf);
    if (!g_inputext) { printf("FAIL: inputext\n"); goto cleanup; }

    printf("Ready. Click or Enter to start, ESC to quit.\n");
    fflush(stdout);

    /* main loop */
    int running = 1;
    while (running) {
        uint64_t cur_ns = zcsr_clock_now_ns();
        double dt = (double)(cur_ns - g_prev_ns) / 1e9;
        g_prev_ns = cur_ns;
        if (dt > 0.1) dt = 0.1;

        /* pump events — input pump MUST come before surface pump */
        zcsr_input_pump(g_input, g_surface);
        zcsr_input_ext_pump(g_inputext, g_surface);
        zcsr_surface_pump_events(g_surface);

        /* get mouse state */
        zcsr_mouse_state mst = zcsr_input_ext_mouse(g_inputext);
        g_mouse_x = mst.x;
        g_mouse_y = mst.y;

        /* detect left mouse click edge */
        int mouse_clicked = ((mst.buttons & ZCSR_MOUSE_LEFT) && !(g_prev_buttons & ZCSR_MOUSE_LEFT));
        g_prev_buttons = mst.buttons;

        /* poll keyboard events */
        int key_enter = 0;
        int key_r = 0;
        int key_esc = 0;
        zcsr_key_event ev;
        while (zcsr_input_poll(g_input, &ev)) {
            if (ev.action != ZCSR_KEY_DOWN) continue;
            if (ev.key == VK_ENTER) key_enter = 1;
            if (ev.key == VK_R) key_r = 1;
            if (ev.key == VK_ESC) key_esc = 1;
        }

        if (key_esc) { running = 0; break; }

        /* state-specific input */
        if (g_state == STATE_MENU) {
            if (mouse_clicked || key_enter) {
                reset_game();
            }
        } else if (g_state == STATE_PLAYING) {
            /* update timer */
            g_time_left -= dt;
            if (g_time_left <= 0.0) {
                g_time_left = 0.0;
                g_state = STATE_RESULT;
                g_target_visible = 0;
            }

            /* handle click */
            if (mouse_clicked) {
                if (g_target_visible && point_in_target(g_mouse_x, g_mouse_y)) {
                    double reaction = now_sec() - g_target_spawn_time;
                    if (g_sample_count < MAX_SAMPLES) {
                        g_reaction_times[g_sample_count++] = reaction;
                    }
                    if (reaction < g_best_reaction) g_best_reaction = reaction;
                    g_hits++;
                    spawn_target();
                } else {
                    g_misses++;
                }
            }
        } else if (g_state == STATE_RESULT) {
            if (key_r) {
                reset_game();
            }
        }

        /* render */
        zcsr_gl_begin(g_renderer);
        zcsr_gl_clear(g_renderer, 0.02f, 0.03f, 0.05f, 1.0f);

        if (g_state == STATE_MENU) {
            render_menu();
        } else if (g_state == STATE_PLAYING) {
            render_playing();
        } else {
            render_result();
        }

        zcsr_gl_flush(g_renderer);
        zcsr_gl_present(g_renderer);
    }

cleanup:
    if (g_inputext) zcsr_input_ext_shutdown(g_inputext);
    if (g_renderer) zcsr_gl_destroy(g_renderer);
    if (g_surface) zcsr_surface_destroy(g_surface);
    printf("Exiting.\n");
    return 0;
}
