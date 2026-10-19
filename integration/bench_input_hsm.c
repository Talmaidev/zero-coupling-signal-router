/* bench_input_hsm.c — input & hsm CPU baseline.
 *
 * Pure CPU, headless. Measures:
 *   input: zcsr_input_is_down query cost (per-frame hot path)
 *   hsm:   zcsr_hsm_dispatch signal cost (state transition + ancestor walk)
 *
 * Build: part of CMake zcsr_input_hsm_bench target.
 */
#include "zcsr/input.h"
#include "zcsr/hsm.h"
#include "zcsr/state_buffer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ITERS 100000

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
static uint64_t now_ns(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER ctr;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&ctr);
    return (uint64_t)((double)ctr.QuadPart * 1e9 / (double)freq.QuadPart);
}
#else
#  include <time.h>
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#endif

/* ---- input bench ---- */

static void bench_input(void) {
    static uint8_t input_buf[65536];
    zcsr_input* in = zcsr_input_init(input_buf, sizeof(input_buf));
    if (!in) { printf("input init FAIL\n"); return; }

    /* Measure is_down: 256 key queries per iteration (simulates a game polling many keys) */
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = now_ns();
        volatile bool sink = false;
        for (int k = 0; k < 256; ++k) {
            sink = zcsr_input_is_down(in, (zcsr_keycode)k);
        }
        (void)sink;
        uint64_t dt = now_ns() - t0;
        if (dt < best) best = dt;
    }
    double ms = (double)best / 1e6;
    printf("input is_down:  %.4f ms/256 queries (%.1f ns/query)\n", ms, (double)best / 256.0);

    /* Measure poll: drain empty queue (worst-case per-frame call) */
    best = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = now_ns();
        zcsr_key_event ev;
        volatile bool got = zcsr_input_poll(in, &ev);
        (void)got;
        uint64_t dt = now_ns() - t0;
        if (dt < best) best = dt;
    }
    printf("input poll:     %.1f ns/call (empty queue)\n", (double)best);
}

/* ---- hsm bench ---- */

static const zcsr_hsm_state hsm_states[] = {
    { "root",       NULL },
    { "menu",       "root" },
    { "menu_main",  "menu" },
    { "menu_opt",   "menu" },
    { "game",       "root" },
    { "game_play",  "game" },
    { "game_pause", "game" },
    { "game_over",  "game" },
};

static const zcsr_hsm_transition hsm_trans[] = {
    { "menu_main",  "start",   "game_play"  },
    { "menu_main",  "options", "menu_opt"   },
    { "menu_opt",   "back",    "menu_main"  },
    { "game_play",  "pause",   "game_pause" },
    { "game_pause", "resume",  "game_play"  },
    { "game_pause", "quit",    "menu_main"  },
    { "game_play",  "die",     "game_over"  },
    { "game_over",  "restart", "game_play"  },
    { "game_over",  "quit",    "menu_main"  },
    /* hierarchical: root handles "reset" from any state */
    { "root",       "reset",   "menu_main"  },
    /* game-level: "pause" handled by game subtree */
    { "game",       "pause",   "game_pause" },
};

static void bench_hsm(void) {
    static uint8_t state_buf[4096];
    zcsr_state* persist = zcsr_state_init(state_buf, sizeof(state_buf));
    if (!persist) { printf("state init FAIL\n"); return; }

    zcsr_hsm* hsm = zcsr_hsm_create(persist,
        hsm_states, sizeof(hsm_states)/sizeof(hsm_states[0]),
        hsm_trans, sizeof(hsm_trans)/sizeof(hsm_trans[0]),
        "game_play");
    if (!hsm) { printf("hsm create FAIL\n"); return; }

    /* Benchmark 1: direct transition (current state has matching rule) */
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        /* reset to game_play first */
        zcsr_hsm_dispatch(hsm, "reset", NULL);
        uint64_t t0 = now_ns();
        volatile bool r = zcsr_hsm_dispatch(hsm, "pause", NULL);
        (void)r;
        uint64_t dt = now_ns() - t0;
        if (dt < best) best = dt;
    }
    printf("hsm dispatch (direct):    %.1f ns/call\n", (double)best);

    /* Benchmark 2: hierarchical transition (walk up to ancestor) */
    best = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        zcsr_hsm_dispatch(hsm, "reset", NULL); /* -> menu_main */
        uint64_t t0 = now_ns();
        /* "reset" is defined on root, must walk up from menu_main -> menu -> root */
        volatile bool r = zcsr_hsm_dispatch(hsm, "reset", NULL);
        (void)r;
        uint64_t dt = now_ns() - t0;
        if (dt < best) best = dt;
    }
    printf("hsm dispatch (ancestor):  %.1f ns/call (walk up 2 levels)\n", (double)best);

    /* Benchmark 3: no transition (signal not matched, full walk) */
    best = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        zcsr_hsm_dispatch(hsm, "reset", NULL);
        uint64_t t0 = now_ns();
        volatile bool r = zcsr_hsm_dispatch(hsm, "nonexistent_signal", NULL);
        (void)r;
        uint64_t dt = now_ns() - t0;
        if (dt < best) best = dt;
    }
    printf("hsm dispatch (no match):  %.1f ns/call (full ancestor walk)\n", (double)best);

    /* Benchmark 4: current() query */
    best = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = now_ns();
        volatile const char* s = zcsr_hsm_current(hsm);
        (void)s;
        uint64_t dt = now_ns() - t0;
        if (dt < best) best = dt;
    }
    printf("hsm current:              %.1f ns/call\n", (double)best);

    zcsr_hsm_destroy(hsm);
}

int main(void) {
    printf("=== input + hsm CPU baseline ===\n");
    bench_input();
    printf("\n");
    bench_hsm();
    printf("=================================\n");
    return 0;
}
