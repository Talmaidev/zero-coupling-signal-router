/* zcsr glrender performance bench.
 *
 * Scenarios:
 *   A. 1000 sprites x 128 textures   (worst batching: ~1000 draw calls)
 *   B. 10000 sprites x 1 texture     (best batching: 1 draw call, MAX_VERTICES zero margin)
 *   C. 1000 sprites x mixed modes    (FUNCTIONAL TEST: same texture merges to 1 draw,
 *                                      mode is vertex attribute, not blend state switch)
 *   D1. instanced sort cost          (pure CPU: zcsr_sort_instances radix sort)
 *
 * Timing: ALL scenarios measure ONLY the CPU submit_batch / sort call.
 * They do NOT include flush, GPU completion, or swap.
 * GPU timing is MISSING (no GL timer query in this bench).
 *
 * Stats: best / p50 / p95 over ITERS samples.
 * Needs a real windowed host for A/B/C. On headless it skips A/B/C but still runs D1.
 * All buffers are caller-provided statics — no runtime heap.
 */
#include "zcsr/gl_render.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_SPRITES   10000
#define MAX_TEXTURES  128
#define MAX_INSTANCES 10000
#define ITERS         240

/* --- cross-platform monotonic clock --- */
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
static uint64_t zcsr_now_ns(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER ctr;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&ctr);
    return (uint64_t)((double)ctr.QuadPart * 1.0e9 / (double)freq.QuadPart);
}
#else
#  include <time.h>
static uint64_t zcsr_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#endif

/* --- stats: collect samples, compute best/p50/p95 --- */
typedef struct {
    double best_ms;
    double p50_ms;
    double p95_ms;
} zcsr_bench_stats;

static int zcsr_cmp_u64(const void* a, const void* b) {
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

static zcsr_bench_stats zcsr_compute_stats(uint64_t* samples, int n) {
    zcsr_bench_stats s;
    qsort(samples, (size_t)n, sizeof(uint64_t), zcsr_cmp_u64);
    s.best_ms = (double)samples[0] / 1e6;
    s.p50_ms  = (double)samples[n / 2] / 1e6;
    s.p95_ms  = (double)samples[(int)(n * 0.95)] / 1e6;
    return s;
}

/* --- bench a sprite batch (CPU submit only, no flush/GPU/swap) --- */
static zcsr_bench_stats zcsr_bench_batch(zcsr_gl_renderer* renderer,
                                          const zcsr_gl_sprite* sprites, size_t count) {
    static uint64_t samples[ITERS];
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = zcsr_now_ns();
        zcsr_gl_submit_batch(renderer, sprites, count);
        samples[i] = zcsr_now_ns() - t0;
    }
    return zcsr_compute_stats(samples, ITERS);
}

/* --- bench instanced sort (pure CPU, no GL) --- */
static zcsr_bench_stats zcsr_bench_sort(zcsr_gl_instance* instances, size_t count,
                                         void* temp, size_t temp_bytes) {
    static uint64_t samples[ITERS];
    /* keep a pristine copy because sort is in-place */
    static zcsr_gl_instance pristine[MAX_INSTANCES];
    for (size_t i = 0; i < count; ++i) pristine[i] = instances[i];

    for (int i = 0; i < ITERS; ++i) {
        for (size_t j = 0; j < count; ++j) instances[j] = pristine[j];
        uint64_t t0 = zcsr_now_ns();
        zcsr_sort_instances(instances, count, temp, temp_bytes);
        samples[i] = zcsr_now_ns() - t0;
    }
    return zcsr_compute_stats(samples, ITERS);
}

/* --- bench instanced submit_batch (CPU submit + sort + draw setup, no swap wait) --- */
static zcsr_bench_stats zcsr_bench_instanced(zcsr_gl_renderer* renderer,
                                              zcsr_gl_instance* instances, size_t count,
                                              void* temp, size_t temp_bytes) {
    static uint64_t samples[ITERS];
    static zcsr_gl_instance pristine[MAX_INSTANCES];
    for (size_t i = 0; i < count; ++i) pristine[i] = instances[i];
    for (int i = 0; i < ITERS; ++i) {
        for (size_t j = 0; j < count; ++j) instances[j] = pristine[j];
        uint64_t t0 = zcsr_now_ns();
        zcsr_gl_submit_instance_batch(renderer, instances, count, temp, temp_bytes);
        samples[i] = zcsr_now_ns() - t0;
    }
    return zcsr_compute_stats(samples, ITERS);
}

static void zcsr_print_stats(const char* label, zcsr_bench_stats s) {
    printf("%-55s best=%8.4f ms  p50=%8.4f ms  p95=%8.4f ms\n", label, s.best_ms, s.p50_ms, s.p95_ms);
}

int main(void) {
    static unsigned char   renderer_buf[8 * 1024 * 1024];
    static zcsr_gl_sprite  sprites[MAX_SPRITES];
    static zcsr_gl_texture textures[MAX_TEXTURES];
    static unsigned char   tex_pixels[MAX_TEXTURES * 2 * 2 * 4];
    static zcsr_gl_instance instances[MAX_INSTANCES];
    static unsigned char    sort_temp[MAX_INSTANCES * sizeof(zcsr_gl_instance)];

    printf("=== zcsr glrender performance bench ===\n");
    printf("Timing: CPU submit/sort ONLY. No flush, no GPU wait, no swap.\n");
    printf("Stats: best / p50 / p95 over %d samples.\n\n", ITERS);

    /* ---- D1: instanced sort cost (pure CPU, runs even on headless) ---- */
    for (int i = 0; i < MAX_INSTANCES; ++i) {
        instances[i] = (zcsr_gl_instance){
            .cx = (float)((i * 17u) % 760u), .cy = (float)((i * 29u) % 560u),
            .w = 8.0f, .h = 8.0f,
            .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
            .r = 1, .g = 1, .b = 1, .a = 1,
            .rotation = 0, .amount = 0, .mode = 0,
            .tex = (uint32_t)((i * 7u) % 128u),  /* spread across 128 textures */
            .z = 0
        };
    }
    zcsr_bench_stats d1 = zcsr_bench_sort(instances, MAX_INSTANCES,
                                           sort_temp, sizeof(sort_temp));
    zcsr_print_stats("D1. instanced sort 10000 instances (128 textures, radix)", d1);

    /* ---- window + renderer for A/B/C ---- */
    zcsr_surface* surface = zcsr_surface_create(
        "zcsr perf bench", (zcsr_rect){ 0, 0, 800, 600 });
    if (!surface) {
        printf("\nSKIP A/B/C: no window surface (headless / no DISPLAY)\n");
        printf("========================================\n");
        return 0;
    }
    zcsr_gl_renderer* renderer = zcsr_gl_create(surface, renderer_buf, sizeof renderer_buf);
    if (!renderer) {
        printf("\nSKIP A/B/C: GL renderer unavailable\n");
        zcsr_surface_destroy(surface);
        return 0;
    }
    zcsr_gl_set_vsync(renderer, false);

    /* create 128 unique textures */
    for (int i = 0; i < MAX_TEXTURES; ++i) {
        unsigned char* px = tex_pixels + (size_t)i * 2 * 2 * 4;
        for (int p = 0; p < 4; ++p) {
            px[p * 4 + 0] = (unsigned char)(i & 0xFF);
            px[p * 4 + 1] = (unsigned char)((i * 2) & 0xFF);
            px[p * 4 + 2] = (unsigned char)((i * 3) & 0xFF);
            px[p * 4 + 3] = 255;
        }
        textures[i] = zcsr_gl_texture_create(renderer, px, 2, 2);
        if (!textures[i]) {
            printf("FAIL: texture %d upload failed\n", i);
            for (int j = 0; j < i; ++j) zcsr_gl_texture_destroy(renderer, textures[j]);
            zcsr_gl_destroy(renderer);
            zcsr_surface_destroy(surface);
            return 1;
        }
    }

    printf("\n");

    /* A: 1000 sprites x 128 textures (worst batching) */
    for (int i = 0; i < 1000; ++i) {
        sprites[i] = (zcsr_gl_sprite){
            textures[i % MAX_TEXTURES],
            { (float)((i * 17u) % 760u), (float)((i * 29u) % 560u), 8.0f, 8.0f },
            0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
            ZCSR_MOD_MULTIPLY, 0.0f
        };
    }
    zcsr_print_stats("A. 1000 sprites x 128 textures (worst batching, ~1000 draws)",
                     zcsr_bench_batch(renderer, sprites, 1000));

    /* B: 10000 sprites x 1 texture (best batching, MAX_VERTICES=60000 zero margin) */
    for (int i = 0; i < 10000; ++i) {
        float t = (float)(i % 255u) / 255.0f;
        sprites[i] = (zcsr_gl_sprite){
            textures[0],
            { (float)((i * 17u) % 760u), (float)((i * 29u) % 560u), 8.0f, 8.0f },
            (float)(i % 64u) * 0.03f,
            0.45f + 0.55f * t, 0.75f, 1.0f - 0.35f * t, 1.0f,
            ZCSR_MOD_MULTIPLY, 0.0f
        };
    }
    zcsr_print_stats("B. 10000 sprites x 1 texture (best batching, 1 draw, MAX_VERTICES zero margin)",
                     zcsr_bench_batch(renderer, sprites, 10000));

    /* C: FUNCTIONAL TEST — same texture, mixed modes.
     * Source fact: flush merge condition is texture==same && first==contiguous,
     * mode is NOT checked. So all 1000 sprites merge into 1 draw call.
     * This measures shader vertex-attribute branching, NOT blend state switching. */
    for (int i = 0; i < 1000; ++i) {
        sprites[i] = (zcsr_gl_sprite){
            textures[0],
            { (float)((i * 17u) % 760u), (float)((i * 29u) % 560u), 8.0f, 8.0f },
            0.0f, 1.0f, 0.5f, 0.5f, 1.0f,
            (zcsr_color_mode)(i % 3), 0.5f
        };
    }
    zcsr_print_stats("C. 1000 sprites x mixed modes [FUNCTIONAL: same tex merges to 1 draw]",
                     zcsr_bench_batch(renderer, sprites, 1000));

    /* D2: instanced single-texture draw (10000 instances, 1 texture = 1 run) */
    for (int i = 0; i < MAX_INSTANCES; ++i) {
        instances[i] = (zcsr_gl_instance){
            .cx = (float)((i * 17u) % 760u), .cy = (float)((i * 29u) % 560u),
            .w = 8, .h = 8, .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1,
            .r = 1, .g = 1, .b = 1, .a = 1,
            .rotation = 0, .amount = 0, .mode = 0,
            .tex = (uint32_t)textures[0], .z = 0
        };
    }
    zcsr_print_stats("D2. instanced 10000 x 1 texture (1 run, state-hoisted)",
                     zcsr_bench_instanced(renderer, instances, MAX_INSTANCES,
                                          sort_temp, sizeof(sort_temp)));

    /* D3: instanced multi-texture draw (10000 instances, 128 textures = ~128 runs) */
    for (int i = 0; i < MAX_INSTANCES; ++i) {
        instances[i].tex = (uint32_t)textures[(i * 7u) % MAX_TEXTURES];
    }
    zcsr_print_stats("D3. instanced 10000 x 128 textures (~128 runs, state-hoisted)",
                     zcsr_bench_instanced(renderer, instances, MAX_INSTANCES,
                                          sort_temp, sizeof(sort_temp)));

    printf("\nNOTE: C is a functional/merge test, not a blend-state-switch perf test.\n");
    printf("========================================\n");

    /* cleanup */
    for (int i = 0; i < MAX_TEXTURES; ++i)
        zcsr_gl_texture_destroy(renderer, textures[i]);
    zcsr_gl_destroy(renderer);
    zcsr_surface_destroy(surface);
    return 0;
}
