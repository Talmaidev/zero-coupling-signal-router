/* Agent 6 — micro-benchmark harness.
 * Targets (spec): single frame render < 1ms; 1000 refreshes/sec; < 0.1 FPS impact. */
#include "zcsr/overlay.h"
#include <stdio.h>
#include <stdint.h>

/* Monotonic timestamp in nanoseconds, cross-platform (Windows has no clock_gettime). */
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
    zcsr_overlay* ovl = zcsr_overlay_create(0);
    if (!ovl) {
        printf("bench: overlay not implemented yet (Agent 4) — skipping render benchmark.\n");
        printf("Validate once ready: < 1ms/frame, >= 1000 refresh/s, < 0.1 FPS impact.\n");
        return 0;
    }
    enum { N = 1000 };
    uint64_t t0 = now_ns();
    for (int i = 0; i < N; ++i) zcsr_overlay_render(ovl);
    uint64_t t1 = now_ns();
    double ms = (double)(t1 - t0) / 1.0e6;
    printf("bench: %d renders in %.3f ms  ->  %.4f ms/frame, %.0f refresh/s\n",
           N, ms, ms / N, N / (ms / 1000.0));
    int pass = (ms / N) < 1.0 && (N / (ms / 1000.0)) >= 1000.0;
    printf("PASS (<1ms/frame && >=1000 refresh/s): %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
