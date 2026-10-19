/* demo_2048.c — 2048 game built on zcsr modules.
 *
 * Reuses game_core.c (bitboard + precomputed move tables) from the 2048 AI project.
 * zcsr provides: platform (window), input (keyboard), glrender (grid+text),
 *                hsm (game states), audiomix (merge sound effects), signal (event routing).
 *
 * Build: cmake target zcsr_2048_demo
 * Run:   ./build/zcsr_2048_demo.exe
 *
 * Controls: Arrow keys / WASD to move. R to restart. Close window to exit.
 */
#include "zcsr/platform.h"
#include "zcsr/gl_render.h"
#include "zcsr/input.h"
#include "zcsr/hsm.h"
#include "zcsr/state_buffer.h"
#include "zcsr/audio_mix.h"
#include "zcsr/signal.h"
#include "game_core.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- constants ---- */
#define WIN_W        560
#define WIN_H        680
#define GRID_SIZE    4
#define CELL_SIZE    120
#define CELL_GAP     10
#define GRID_MARGIN  30
#define GRID_TOP     120  /* leave room for score display */
#define MAX_FRAMES   7200 /* 2min safety timeout */

/* ---- colors for each tile value (index = log2(value), 0=empty) ---- */
static const float TILE_COLORS[12][3] = {
    {0.73f, 0.68f, 0.63f},  /* 0 (empty) */
    {0.93f, 0.89f, 0.85f},  /* 2 */
    {0.93f, 0.88f, 0.78f},  /* 4 */
    {0.95f, 0.69f, 0.47f},  /* 8 */
    {0.96f, 0.58f, 0.39f},  /* 16 */
    {0.97f, 0.49f, 0.37f},  /* 32 */
    {0.96f, 0.37f, 0.23f},  /* 64 */
    {0.93f, 0.81f, 0.44f},  /* 128 */
    {0.93f, 0.80f, 0.38f},  /* 256 */
    {0.93f, 0.78f, 0.31f},  /* 512 */
    {0.93f, 0.77f, 0.25f},  /* 1024 */
    {0.93f, 0.76f, 0.18f},  /* 2048 */
};

/* ---- compile-time signal router: game events -> audiomix ---- */
#define GAME_CONNS(X) \
    X("PLAY_MERGE",  zcsr_mix_slot_play) \
    X("PLAY_MOVE",   zcsr_mix_slot_play) \
    X("PLAY_GAMEOVER", zcsr_mix_slot_play)
ZCSR_DEFINE_ROUTER(game, GAME_CONNS)

/* ---- HSM states ---- */
static const zcsr_hsm_state hsm_states[] = {
    { "menu",      NULL },
    { "playing",   NULL },
    { "game_over", NULL },
};
static const zcsr_hsm_transition hsm_trans[] = {
    { "menu",      "start",    "playing"   },
    { "playing",   "gameover", "game_over" },
    { "game_over", "restart",  "playing"   },
    { "game_over", "quit",     "menu"      },
};

/* ---- caller-owned statics (no heap) ---- */
static unsigned char g_renderer_buf[8 * 1024 * 1024];
static unsigned char g_input_buf[4 * 1024];
static unsigned char g_state_buf[4096];

static zcsr_surface*      g_surface;
static zcsr_gl_renderer*  g_renderer;
static zcsr_input*        g_input;
static zcsr_hsm*          g_hsm;
static zcsr_state*        g_state;
static zcsr_mixer*        g_mixer;

static board_t g_board;
static int     g_score;
static int     g_best_score;
static bool    g_audio_ok;

/* ---- tile color lookup ---- */
static void tile_color(int value, float* r, float* g, float* b) {
    int idx = 0;
    if (value > 0) {
        int v = value;
        while (v > 1) { v >>= 1; idx++; }
    }
    if (idx >= 12) idx = 11;
    *r = TILE_COLORS[idx][0];
    *g = TILE_COLORS[idx][1];
    *b = TILE_COLORS[idx][2];
}

/* ---- text color: dark for light tiles, light for dark tiles ---- */
static void text_color(int value, float* r, float* g, float* b) {
    if (value <= 4) { *r = 0.47f; *g = 0.43f; *b = 0.40f; }
    else            { *r = 0.98f; *g = 0.98f; *b = 0.98f; }
}

/* ---- render the game ---- */
static void render_game(void) {
    zcsr_gl_clear(g_renderer, 0.98f, 0.97f, 0.94f, 1.0f);
    zcsr_gl_begin(g_renderer);

    /* score panel background */
    zcsr_gl_fill_rect(g_renderer, 0, 0, (float)WIN_W, (float)GRID_TOP,
                      0.90f, 0.87f, 0.82f, 1.0f);

    /* title */
    zcsr_gl_draw_text(g_renderer, "2048", 30, 25, 3.0f, 0.47f, 0.43f, 0.40f, 1.0f);

    /* score boxes */
    char score_buf[32];
    snprintf(score_buf, sizeof(score_buf), "%d", g_score);
    zcsr_gl_fill_rect(g_renderer, 340, 20, 90, 55, 0.73f, 0.68f, 0.63f, 1.0f);
    zcsr_gl_draw_text(g_renderer, "SCORE", 350, 25, 0.8f, 0.93f, 0.89f, 0.85f, 1.0f);
    zcsr_gl_draw_text(g_renderer, score_buf, 350, 45, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f);

    snprintf(score_buf, sizeof(score_buf), "%d", g_best_score);
    zcsr_gl_fill_rect(g_renderer, 445, 20, 90, 55, 0.73f, 0.68f, 0.63f, 1.0f);
    zcsr_gl_draw_text(g_renderer, "BEST", 455, 25, 0.8f, 0.93f, 0.89f, 0.85f, 1.0f);
    zcsr_gl_draw_text(g_renderer, score_buf, 455, 45, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f);

    /* grid background */
    float grid_x = GRID_MARGIN;
    float grid_y = GRID_TOP;
    float grid_w = (float)(GRID_SIZE * CELL_SIZE + (GRID_SIZE - 1) * CELL_GAP);
    float grid_h = grid_w;
    zcsr_gl_fill_rect(g_renderer, grid_x - 10, grid_y - 10, grid_w + 20, grid_h + 20,
                      0.73f, 0.68f, 0.63f, 1.0f);

    /* cells */
    int values[16];
    game_get_board_values(g_board, values);
    for (int row = 0; row < GRID_SIZE; row++) {
        for (int col = 0; col < GRID_SIZE; col++) {
            int idx = row * GRID_SIZE + col;
            int val = values[idx];
            float x = grid_x + col * (CELL_SIZE + CELL_GAP);
            float y = grid_y + row * (CELL_SIZE + CELL_GAP);

            float tr, tg, tb;
            tile_color(val, &tr, &tg, &tb);
            zcsr_gl_fill_rect(g_renderer, x, y, CELL_SIZE, CELL_SIZE, tr, tg, tb, 1.0f);

            if (val > 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", val);
                float scale = (val >= 1024) ? 1.2f : (val >= 128) ? 1.5f : 2.0f;
                /* center text approximately */
                float text_w = (float)strlen(buf) * 8.0f * scale;
                float tx = x + (CELL_SIZE - text_w) / 2.0f;
                float ty = y + (CELL_SIZE - 12.0f * scale) / 2.0f;
                float cr, cg, cb;
                text_color(val, &cr, &cg, &cb);
                zcsr_gl_draw_text(g_renderer, buf, tx, ty, scale, cr, cg, cb, 1.0f);
            }
        }
    }

    /* hint text */
    zcsr_gl_draw_text(g_renderer, "Arrow keys / WASD to move | R to restart",
                      30, WIN_H - 35, 0.9f, 0.47f, 0.43f, 0.40f, 1.0f);

    zcsr_gl_flush(g_renderer);
}

/* ---- render menu overlay ---- */
static void render_menu(void) {
    render_game();
    /* semi-transparent overlay */
    zcsr_gl_fill_rect(g_renderer, 0, 0, (float)WIN_W, (float)WIN_H, 0.0f, 0.0f, 0.0f, 0.5f);
    zcsr_gl_draw_text(g_renderer, "2048", 180, 200, 5.0f, 0.93f, 0.76f, 0.18f, 1.0f);
    zcsr_gl_draw_text(g_renderer, "Press SPACE to start", 130, 340, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f);
    zcsr_gl_draw_text(g_renderer, "Arrow keys / WASD to move", 110, 390, 1.2f, 0.9f, 0.9f, 0.9f, 1.0f);
    zcsr_gl_flush(g_renderer);
}

/* ---- render game over overlay ---- */
static void render_game_over(void) {
    render_game();
    zcsr_gl_fill_rect(g_renderer, 0, 0, (float)WIN_W, (float)WIN_H, 0.0f, 0.0f, 0.0f, 0.6f);
    zcsr_gl_draw_text(g_renderer, "GAME OVER", 100, 220, 3.5f, 0.97f, 0.49f, 0.37f, 1.0f);
    char buf[64];
    snprintf(buf, sizeof(buf), "Score: %d", g_score);
    zcsr_gl_draw_text(g_renderer, buf, 170, 310, 2.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    zcsr_gl_draw_text(g_renderer, "Press R to restart", 130, 380, 1.3f, 0.9f, 0.9f, 0.9f, 1.0f);
    zcsr_gl_flush(g_renderer);
}

/* ---- start a new game ---- */
static void new_game(void) {
    game_core_init();
    g_board = game_new_board();
    g_score = 0;
}

/* ---- handle a move ---- */
static void do_move(int direction) {
    if (strcmp(zcsr_hsm_current(g_hsm), "playing") != 0) return;

    int gained = game_execute_move(&g_board, direction);
    if (gained < 0) return;  /* invalid move */

    g_score += gained;
    if (g_score > g_best_score) g_best_score = g_score;

    g_board = game_add_random_tile(g_board);

    if (gained > 0 && g_audio_ok) {
        game_emit("PLAY_MERGE", "merge");
    } else if (g_audio_ok) {
        game_emit("PLAY_MOVE", "move");
    }

    if (game_is_game_over(g_board)) {
        if (g_audio_ok) game_emit("PLAY_GAMEOVER", "gameover");
        zcsr_hsm_dispatch(g_hsm, "gameover", NULL);
    }
}

/* ---- key mapping ---- */
static int key_to_direction(zcsr_keycode key) {
    /* Common keycodes: arrow keys and WASD */
    switch (key) {
        case 38:  return 0;  /* UP arrow */
        case 40:  return 1;  /* DOWN arrow */
        case 37:  return 2;  /* LEFT arrow */
        case 39:  return 3;  /* RIGHT arrow */
        case 87:  return 0;  /* W */
        case 83:  return 1;  /* S */
        case 65:  return 2;  /* A */
        case 68:  return 3;  /* D */
        default:  return -1;
    }
}

int main(void) {
    printf("=== zcsr 2048 ===\n");

    /* ---- init game core ---- */
    game_core_init();
    g_board = game_new_board();
    g_score = 0;
    g_best_score = 0;

    /* ---- window + renderer ---- */
    g_surface = zcsr_surface_create("zcsr 2048", (zcsr_rect){ 0, 0, WIN_W, WIN_H });
    if (!g_surface) { printf("SKIP: no window surface\n"); return 0; }

    g_renderer = zcsr_gl_create(g_surface, g_renderer_buf, sizeof g_renderer_buf);
    if (!g_renderer) {
        printf("SKIP: GL renderer unavailable\n");
        zcsr_surface_destroy(g_surface);
        return 0;
    }
    zcsr_gl_set_vsync(g_renderer, true);

    g_input = zcsr_input_init(g_input_buf, sizeof g_input_buf);
    g_state = zcsr_state_init(g_state_buf, sizeof g_state_buf);
    g_hsm = zcsr_hsm_create(g_state, hsm_states, 3, hsm_trans, 4, "menu");

    /* ---- audio (optional, fail gracefully) ---- */
    g_mixer = zcsr_mix_start();
    g_audio_ok = false;
    if (g_mixer) {
        /* Try to register sound effects; paths are relative to build/ */
        bool ok = true;
        ok &= zcsr_mix_register_wav(g_mixer, "merge",    "../integration/sfx_merge.wav");
        ok &= zcsr_mix_register_wav(g_mixer, "move",     "../integration/sfx_move.wav");
        ok &= zcsr_mix_register_wav(g_mixer, "gameover", "../integration/sfx_gameover.wav");
        g_audio_ok = ok;
        printf("[audio] mixer %s\n", ok ? "ready" : "WARN: sound effects not found");
    } else {
        printf("[audio] mixer unavailable (continuing without sound)\n");
    }

    printf("[2048] ready. Arrow keys/WASD to move, R to restart.\n");

    /* ---- main loop ---- */
    bool running = true;
    for (int frame = 0; running && frame < MAX_FRAMES; ++frame) {
        zcsr_surface_pump_events(g_surface);

        if (g_input) {
            zcsr_input_pump(g_input, g_surface);
            zcsr_key_event ev;
            while (zcsr_input_poll(g_input, &ev)) {
                if (ev.action != ZCSR_KEY_DOWN) continue;

                const char* state = zcsr_hsm_current(g_hsm);

                if (strcmp(state, "menu") == 0) {
                    if (ev.key == 32 /* SPACE */ || ev.key == 13 /* ENTER */) {
                        new_game();
                        zcsr_hsm_dispatch(g_hsm, "start", NULL);
                    }
                } else if (strcmp(state, "playing") == 0) {
                    int dir = key_to_direction(ev.key);
                    if (dir >= 0) {
                        do_move(dir);
                    } else if (ev.key == 82 /* R */) {
                        new_game();
                    }
                } else if (strcmp(state, "game_over") == 0) {
                    if (ev.key == 82 /* R */ || ev.key == 32 /* SPACE */) {
                        new_game();
                        zcsr_hsm_dispatch(g_hsm, "restart", NULL);
                    }
                }
            }
        }

        /* ---- render based on state ---- */
        const char* state = zcsr_hsm_current(g_hsm);
        if (strcmp(state, "menu") == 0) {
            render_menu();
        } else if (strcmp(state, "game_over") == 0) {
            render_game_over();
        } else {
            render_game();
        }
        zcsr_gl_present(g_renderer);
    }

    /* ---- cleanup ---- */
    printf("[2048] shutting down...\n");
    if (g_mixer) zcsr_mix_stop(g_mixer);
    if (g_hsm) zcsr_hsm_destroy(g_hsm);
    zcsr_gl_destroy(g_renderer);
    zcsr_surface_destroy(g_surface);
    printf("[2048] done. Final score: %d\n", g_score);
    return 0;
}
