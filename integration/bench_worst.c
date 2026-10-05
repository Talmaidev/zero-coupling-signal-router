/* Worst-case GPU render bench — measures glrender under adversarial conditions.
 *
 * Scenarios:
 *   A. 1000 sprites x 1000 unique textures  (worst-case batching: ~1000 draw calls)
 *   B. 10000 sprites x 1 texture             (best-case batching: 1 draw call)
 *   C. 1000 sprites x mixed blend modes      (blend state changes break batching)
 *
 * Needs a real windowed host. On headless boxes it skips with exit 0.
 * All buffers are caller-provided statics — no runtime heap.
 */
#include "zcsr/gl_render.h"

#include <stdint.h>
#include <stdio.h>

#define MAX_SPRITES  10000
#define MAX_TEXTURES 128   /* glrender internal texture pool cap: ZCSR_GL_MAX_TEXTURES */
#define ITERS        240

/* --- cross-platform monotonic clock (Windows has no clock_gettime) --- */
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

/* --- best-of-N frame time for a sprite batch --- */
static double zcsr_bench_batch(zcsr_gl_renderer* renderer,
                                const zcsr_gl_sprite* sprites, size_t count) {
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = zcsr_now_ns();
        zcsr_gl_submit_batch(renderer, sprites, count);
        uint64_t dt = zcsr_now_ns() - t0;
        if (dt < best) best = dt;
    }
    return (double)best / 1e6;
}

int main(void) {
    /* ================================================================
     * Caller-provided buffers (no heap):
     *   renderer_buf : 8 MiB (renderer state ~2.3 MiB, headroom for safety)
     *   sprites      : 10000 x zcsr_gl_sprite (~48 bytes each = ~480 KiB)
     *   textures     : 1000 x zcsr_gl_texture (uint32_t = 4 KiB)
     *   tex_pixels   : 1000 x 2x2 x RGBA = 16000 bytes
     * ================================================================ */
    static unsigned char  renderer_buf[8 * 1024 * 1024];
    static zcsr_gl_sprite sprites[MAX_SPRITES];
    static zcsr_gl_texture textures[MAX_TEXTURES];
    static unsigned char   tex_pixels[MAX_TEXTURES * 2 * 2 * 4];

    /* --- create window + renderer --- */
    zcsr_surface* surface = zcsr_surface_create(
        "zcsr worst-case bench", (zcsr_rect){ 0, 0, 800, 600 });
    if (!surface) {
        printf("SKIP: no window surface (headless / no DISPLAY)\n");
        return 0;
    }

    zcsr_gl_renderer* renderer = zcsr_gl_create(surface, renderer_buf, sizeof renderer_buf);
    if (!renderer) {
        printf("SKIP: GL renderer unavailable on this surface/backend\n");
        zcsr_surface_destroy(surface);
        return 0;
    }
    zcsr_gl_set_vsync(renderer, false);

    /* --- create 1000 unique textures (each 2x2, distinct RGB) --- */
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

    /* ================================================================
     * Scenario A: 1000 sprites x 128 unique textures (worst case)
     * Textures are cycled (i % 128) so every adjacent sprite has a
     * different texture -> adjacent-merge cannot combine -> ~1000
     * draw calls. 128 is the glrender internal texture pool cap.
     * ================================================================ */
    for (int i = 0; i < 1000; ++i) {
        sprites[i] = (zcsr_gl_sprite){
            textures[i % MAX_TEXTURES],
            { (float)((i * 17u) % 760u), (float)((i * 29u) % 560u), 8.0f, 8.0f },
            0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
            ZCSR_MOD_MULTIPLY, 0.0f
        };
    }
    double ms_A = zcsr_bench_batch(renderer, sprites, 1000);

    /* ================================================================
     * Scenario B: 10000 sprites x 1 texture (best case)
     * All sprites use textures[0] -> adjacent-merge combines all ->
     * 1 draw call.
     * ================================================================ */
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
    double ms_B = zcsr_bench_batch(renderer, sprites, 10000);

    /* ================================================================
     * Scenario C: 1000 sprites x mixed blend modes (state changes)
     * Alternate MULTIPLY / MIX / ADD -> blend-mode switch breaks
     * adjacent-merge even though all sprites share one texture.
     * ================================================================ */
    for (int i = 0; i < 1000; ++i) {
        sprites[i] = (zcsr_gl_sprite){
            textures[0],
            { (float)((i * 17u) % 760u), (float)((i * 29u) % 560u), 8.0f, 8.0f },
            0.0f, 1.0f, 0.5f, 0.5f, 1.0f,
            (zcsr_color_mode)(i % 3), 0.5f
        };
    }
    double ms_C = zcsr_bench_batch(renderer, sprites, 1000);

    /* --- output --- */
    printf("=== zcsr glrender worst-case bench ===\n");
    printf("A. 1000 sprites x 128 textures  (worst batching): %.4f ms\n", ms_A);
    printf("B. 10000 sprites x 1 texture    (best batching):  %.4f ms\n", ms_B);
    printf("C. 1000 sprites x mixed blend   (state changes):  %.4f ms\n", ms_C);
    printf("A/B ratio: %.2fx  (worst vs best per-sprite)\n",
           (ms_A / 1000.0) / (ms_B / 10000.0));
    printf("=======================================\n");

    /* --- cleanup --- */
    for (int i = 0; i < MAX_TEXTURES; ++i)
        zcsr_gl_texture_destroy(renderer, textures[i]);
    zcsr_gl_destroy(renderer);
    zcsr_surface_destroy(surface);
    return 0;
}
