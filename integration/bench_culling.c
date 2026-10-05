/* Culling bench — linear vs uniform grid across view sizes.
 *
 * 10000 boxes spread over a 4000x4000 world. The view shrinks from covering the whole world
 * (dense, linear-friendly) to a small window (sparse, grid skips most boxes).
 *
 * Pure CPU, headless. All memory caller-provided.
 */
#include "zcsr/culling.h"

#include <stdint.h>
#include <stdio.h>

#define NBOX     10000
#define WORLD    4000
#define CELL     100
#define ITERS    200

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

static double bench_linear(const zcsr_aabb_t* boxes, const zcsr_view_t* view,
                           uint32_t* out, size_t* visible) {
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = now_ns();
        *visible = zcsr_cull_linear(boxes, NBOX, view, out, NBOX);
        uint64_t dt = now_ns() - t0;
        if (dt < best) best = dt;
    }
    return (double)best / 1e6;
}

static double bench_grid(const zcsr_grid_t* g, const zcsr_aabb_t* boxes,
                         const zcsr_view_t* view, uint32_t* out, uint8_t* seen,
                         size_t* visible) {
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = now_ns();
        *visible = zcsr_grid_query(g, boxes, NBOX, view, out, NBOX, seen);
        uint64_t dt = now_ns() - t0;
        if (dt < best) best = dt;
    }
    return (double)best / 1e6;
}

int main(void) {
    static zcsr_aabb_t boxes[NBOX];
    static uint32_t out[NBOX];
    static uint8_t seen[NBOX];
    static uint32_t head[(WORLD / CELL) * (WORLD / CELL)];
    static uint32_t next[NBOX];
    zcsr_grid_t grid = { 0, 0, CELL, CELL, WORLD / CELL, WORLD / CELL, head, next };

    for (int i = 0; i < NBOX; ++i) {
        float x = (float)((i * 613u) % WORLD);
        float y = (float)((i * 997u) % WORLD);
        boxes[i] = (zcsr_aabb_t){ x, y, x + 16, y + 16 };
    }
    zcsr_grid_build(&grid, boxes, NBOX);

    printf("=== culling bench (10000 boxes, 4000 world) ===\n");
    printf("%-12s %-10s %-10s %-10s %-8s\n",
           "view", "visible", "linear ms", "grid ms", "speedup");
    float sizes[] = { 4000, 2000, 1000, 500, 200, 100 };
    for (int s = 0; s < 6; ++s) {
        float v = sizes[s];
        zcsr_view_t view = { 0, 0, v, v };
        size_t vis_l, vis_g;
        double ml = bench_linear(boxes, &view, out, &vis_l);
        double mg = bench_grid(&grid, boxes, &view, out, seen, &vis_g);
        printf("%-12.0f %-10zu %-10.4f %-10.4f %-8.2f\n",
               v, vis_l, ml, mg, ml / mg);
    }
    printf("=================================================\n");
    return 0;
}
