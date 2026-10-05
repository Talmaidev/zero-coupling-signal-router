/* Atlas end-to-end bench — measures the real gain from packing many small images into one atlas.
 *
 * Two paths draw the same 1000 sprites drawn from 64 distinct images:
 *   UNPACKED: 64 separate textures, sprites cycle through them -> texture switches break merging
 *   PACKED:   images packed into one atlas, sprites use zcsr_gl_atlas_sprite with region UV ->
 *             one bound texture -> one draw call
 *
 * Needs a real windowed host; skips with exit 0 when headless. All buffers caller-provided.
 */
#include "zcsr/gl_render.h"
#include "zcsr/texture_atlas.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define N_IMAGES    64
#define IMG_W       8
#define IMG_H       8
#define N_SPRITES   1000
#define ATLAS_CAP_W 128
#define ATLAS_CAP_H 128
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
    static uint8_t small_pixels[N_IMAGES * IMG_W * IMG_H * 4];
    static uint8_t atlas_pixels[ATLAS_CAP_W * ATLAS_CAP_H * 4];
    static zcsr_atlas_item      items[N_IMAGES];
    static zcsr_atlas_placement placements[N_IMAGES];
    static zcsr_atlas_region    regions[N_IMAGES];
    static zcsr_gl_texture      sep_tex[N_IMAGES];
    static zcsr_gl_sprite       sprites[N_SPRITES];
    static zcsr_gl_atlas_sprite asprites[N_SPRITES];

    zcsr_surface* surface = zcsr_surface_create(
        "zcsr atlas bench", (zcsr_rect){ 0, 0, 800, 600 });
    if (!surface) { printf("SKIP: no window surface\n"); return 0; }

    zcsr_gl_renderer* renderer = zcsr_gl_create(surface, renderer_buf, sizeof renderer_buf);
    if (!renderer) {
        printf("SKIP: GL renderer unavailable\n");
        zcsr_surface_destroy(surface);
        return 0;
    }
    zcsr_gl_set_vsync(renderer, false);

    /* build 64 distinct 8x8 images and upload them as separate textures */
    for (int i = 0; i < N_IMAGES; ++i) {
        uint8_t* px = small_pixels + (size_t)i * IMG_W * IMG_H * 4;
        for (int p = 0; p < IMG_W * IMG_H; ++p) {
            px[p * 4 + 0] = (uint8_t)(i * 4);
            px[p * 4 + 1] = (uint8_t)(i * 4 + 80);
            px[p * 4 + 2] = (uint8_t)(255 - i * 4);
            px[p * 4 + 3] = 255;
        }
        sep_tex[i] = zcsr_gl_texture_create(renderer, px, IMG_W, IMG_H);
        if (!sep_tex[i]) { printf("FAIL: sep texture %d\n", i); return 1; }
    }

    /* pack the same 64 images into one atlas */
    for (int i = 0; i < N_IMAGES; ++i) {
        items[i].id = (uint32_t)i;
        items[i].w = IMG_W;
        items[i].h = IMG_H;
    }
    uint16_t used_w = 0, used_h = 0;
    uint32_t packed = zcsr_atlas_pack(items, N_IMAGES, ATLAS_CAP_W, ATLAS_CAP_H,
                                      placements, &used_w, &used_h);
    if (packed != N_IMAGES) { printf("FAIL: only %u packed\n", packed); return 1; }

    /* compose atlas pixels and upload once */
    memset(atlas_pixels, 0, sizeof atlas_pixels);
    for (int i = 0; i < N_IMAGES; ++i) {
        const uint8_t* src = small_pixels + (size_t)items[i].id * IMG_W * IMG_H * 4;
        zcsr_atlas_blit(atlas_pixels, used_w,
                        placements[i].x, placements[i].y,
                        src, IMG_W, IMG_H);
    }
    zcsr_gl_texture atlas_tex = zcsr_gl_texture_create(renderer, atlas_pixels, used_w, used_h);
    if (!atlas_tex) { printf("FAIL: atlas texture\n"); return 1; }

    /* build sprite arrays: same screen layout, cycling the 64 images */
    zcsr_atlas_build_regions(items, placements, N_IMAGES, used_w, used_h, regions);
    for (int i = 0; i < N_SPRITES; ++i) {
        float x = (float)((i * 13u) % 740u);
        float y = (float)((i * 19u) % 540u);
        int img = i % N_IMAGES;
        sprites[i] = (zcsr_gl_sprite){
            sep_tex[img], { x, y, IMG_W, IMG_H }, 0.0f,
            1, 1, 1, 1, ZCSR_MOD_MULTIPLY, 0.0f
        };
        asprites[i] = (zcsr_gl_atlas_sprite){
            atlas_tex, regions[img], { x, y, IMG_W, IMG_H }, 0.0f,
            1, 1, 1, 1, ZCSR_MOD_MULTIPLY, 0.0f
        };
    }

    /* bench: unpacked (separate textures) */
    uint64_t best_un = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = now_ns();
        zcsr_gl_submit_batch(renderer, sprites, N_SPRITES);
        uint64_t dt = now_ns() - t0;
        if (dt < best_un) best_un = dt;
    }

    /* bench: packed (atlas) */
    uint64_t best_pk = UINT64_MAX;
    for (int i = 0; i < ITERS; ++i) {
        uint64_t t0 = now_ns();
        zcsr_gl_submit_atlas_batch(renderer, asprites, N_SPRITES);
        uint64_t dt = now_ns() - t0;
        if (dt < best_pk) best_pk = dt;
    }

    double ms_un = (double)best_un / 1e6;
    double ms_pk = (double)best_pk / 1e6;
    printf("=== atlas end-to-end bench ===\n");
    printf("UNPACKED (64 separate textures): %.4f ms\n", ms_un);
    printf("PACKED   (1 atlas texture):      %.4f ms\n", ms_pk);
    printf("speedup: %.2fx\n", ms_un / ms_pk);
    printf("atlas size: %ux%u\n", used_w, used_h);
    printf("==============================\n");

    zcsr_gl_destroy(renderer);
    zcsr_surface_destroy(surface);
    return 0;
}
