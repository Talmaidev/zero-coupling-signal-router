/* Instanced rendering bench — compares the legacy per-sprite vertex path against the
 * instanced path for the same 10000 same-texture sprites.
 *
 * LEGACY:  zcsr_gl_submit_batch   -> 6 vertices per sprite (60000 verts, 2.16 MB upload)
 * INSTANCED: zcsr_gl_draw_instances -> 60 bytes per instance (640 KB upload, quad expanded on GPU)
 *
 * Needs a real windowed host; skips headless. All buffers caller-provided.
 */
#include "zcsr/gl_render.h"

#include <stdint.h>
#include <stdio.h>

#define N_COUNT     10000
#define ITERS       240

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
    return (uint64_t)((double)ctr.QuadPart * 1.0e9 / (double)freq.QuadPart);
}
#else
#  include <time.h>
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#endif

int main(void) {
    static unsigned char renderer_buf[8 * 1024 * 1024];
    static zcsr_gl_sprite   sprites[N_COUNT];
    static zcsr_gl_instance instances[N_COUNT];
    static zcsr_gl_instance scratch[N_COUNT];
    static const unsigned char rgba[16] = {
        255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255
    };

    zcsr_surface* surface = zcsr_surface_create("zcsr inst bench",
                                                 (zcsr_rect){0,0,800,600});
    if (!surface) { printf("SKIP: no surface\n"); return 0; }
    zcsr_gl_renderer* renderer = zcsr_gl_create(surface, renderer_buf, sizeof renderer_buf);
    if (!renderer) { printf("SKIP: no renderer\n"); zcsr_surface_destroy(surface); return 0; }
    zcsr_gl_set_vsync(renderer, false);

    zcsr_gl_texture tex = zcsr_gl_texture_create(renderer, rgba, 2, 2);
    if (!tex) { printf("FAIL: texture\n"); return 1; }

    for (int i = 0; i < N_COUNT; ++i) {
        float x = (float)((i * 17u) % 760u);
        float y = (float)((i * 29u) % 560u);
        sprites[i] = (zcsr_gl_sprite){
            tex, { x, y, 8.0f, 8.0f }, 0.0f, 1, 1, 1, 1, ZCSR_MOD_MULTIPLY, 0.0f
        };
        instances[i] = (zcsr_gl_instance){
            x + 4.0f, y + 4.0f, 8.0f, 8.0f,
            0.0f, 0.0f, 1.0f, 1.0f,
            1, 1, 1, 1, 0.0f, 0.0f, 0.0f, tex
        };
    }

    /* legacy path */
    uint64_t best_legacy = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = now_ns();
        zcsr_gl_submit_batch(renderer, sprites, N_COUNT);
        uint64_t dt = now_ns() - t0;
        if (dt < best_legacy) best_legacy = dt;
    }

    /* instanced path (single texture, no sorting needed) */
    uint64_t best_inst = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = now_ns();
        zcsr_gl_draw_instances(renderer, tex, instances, N_COUNT);
        zcsr_gl_present(renderer);
        uint64_t dt = now_ns() - t0;
        if (dt < best_inst) best_inst = dt;
    }

    /* sorted multi-texture batch path (reuse instances, all same tex -> 1 run) */
    uint64_t best_sorted = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = now_ns();
        zcsr_gl_submit_instance_batch(renderer, instances, N_COUNT, scratch, sizeof scratch);
        uint64_t dt = now_ns() - t0;
        if (dt < best_sorted) best_sorted = dt;
    }

    double ms_legacy = (double)best_legacy / 1e6;
    double ms_inst   = (double)best_inst / 1e6;
    double ms_sorted = (double)best_sorted / 1e6;
    printf("=== instanced bench (10000 sprites) ===\n");
    printf("LEGACY (6 verts/sprite):        %.4f ms\n", ms_legacy);
    printf("INSTANCED (64 bytes/instance):  %.4f ms\n", ms_inst);
    printf("SORTED BATCH (sort + instance): %.4f ms\n", ms_sorted);
    printf("instanced vs legacy: %.2fx\n", ms_legacy / ms_inst);
    printf("=======================================\n");

    zcsr_gl_destroy(renderer);
    zcsr_surface_destroy(surface);
    return 0;
}
