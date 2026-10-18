#include "zcsr/gl_render.h"

#include <stdio.h>

int main(void) {
    static unsigned char renderer_buf[8 * 1024 * 1024];
    static const unsigned char rgba[16] = {
        255, 255, 255, 255, 255,   0,   0, 255,
          0, 255,   0, 255,   0,   0, 255, 255
    };

    if (zcsr_gl_create(0, renderer_buf, sizeof renderer_buf)) {
        printf("FAIL: null surface should not create a GL renderer\n");
        return 1;
    }
    if (zcsr_gl_create((zcsr_surface*)1, renderer_buf, 1)) {
        printf("FAIL: undersized caller buffer should not create a GL renderer\n");
        return 1;
    }

    /* NULL-safety: the public API is called every frame from game loops — must no-op on a NULL
     * renderer (and return invalid for value-returning calls), never deref. Runs headless. */
    zcsr_gl_destroy(0);
    zcsr_gl_set_vsync(0, true);
    zcsr_gl_begin(0);
    zcsr_gl_submit(0, 0);
    zcsr_gl_submit_batch(0, 0, 0);
    zcsr_gl_flush(0);
    zcsr_gl_set_target(0, 0);
    zcsr_gl_texture_filter(0, 0, true);
    zcsr_gl_texture_destroy(0, 0);
    zcsr_gl_draw_text(0, "x", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    if (zcsr_gl_texture_create(0, rgba, 2, 2) != 0) {
        printf("FAIL: texture_create(NULL renderer) must return 0\n");
        return 1;
    }
    if (zcsr_gl_texture_upload(0, 1, rgba, 2, 2)) {
        printf("FAIL: texture_upload(NULL renderer) must return false\n");
        return 1;
    }

    /* ---- instanced sort correctness (pure CPU, runs headless) ----
     * Reference: stable insertion sort by tex. Compare with zcsr_sort_instances
     * (radix LSD) to verify ping-pong optimization preserves order and stability. */
    {
        enum { N = 200 };
        static zcsr_gl_instance a[N], ref[N], scratch[N];
        static const uint32_t seeds[N] = {
            3,1,4,1,5,9,2,6,5,3,5,8,9,7,9,3,2,3,8,4,
            6,2,6,4,3,3,8,3,2,7,9,5,0,2,8,8,4,1,9,7,
            1,6,9,3,9,9,3,7,5,1,0,5,8,2,0,9,7,4,9,4,
            4,5,9,2,3,0,7,8,1,6,4,0,6,2,8,6,2,0,8,9,
            9,8,6,2,8,0,3,4,8,2,5,3,4,2,1,1,7,0,6,7,
            9,3,7,2,1,0,4,9,4,4,5,9,2,3,0,7,8,1,6,4,
            0,6,2,8,6,2,0,8,9,9,8,6,2,8,0,3,4,8,2,5,
            3,4,2,1,1,7,0,6,7,9,3,7,2,1,0,4,9,4,4,5,
            9,2,3,0,7,8,1,6,4,0,6,2,8,6,2,0,8,9,9,8,
            6,2,8,0,3,4,8,2,5,3,4,2,1,1,7,0,6,7,9,3
        };
        for (int i = 0; i < N; ++i) {
            a[i] = (zcsr_gl_instance){ .tex = seeds[i], .z = (float)i, .cx = (float)i };
            ref[i] = a[i];
        }
        /* reference stable insertion sort by tex */
        for (int i = 1; i < N; ++i) {
            zcsr_gl_instance key = ref[i];
            int j = i - 1;
            while (j >= 0 && ref[j].tex > key.tex) { ref[j+1] = ref[j]; --j; }
            ref[j+1] = key;
        }
        /* radix sort */
        size_t runs = zcsr_sort_instances(a, N, scratch, sizeof(scratch));
        /* compare: same tex groups, same relative order within group (stability) */
        int ok = 1;
        for (int i = 0; i < N; ++i) {
            if (a[i].tex != ref[i].tex || a[i].cx != ref[i].cx) { ok = 0; break; }
        }
        if (!ok) {
            printf("FAIL: radix sort does not match reference stable sort\n");
            return 1;
        }
        /* count distinct texture runs */
        size_t expected_runs = 1;
        for (int i = 1; i < N; ++i) if (ref[i].tex != ref[i-1].tex) ++expected_runs;
        if (runs != expected_runs) {
            printf("FAIL: radix sort runs=%zu expected=%zu\n", runs, expected_runs);
            return 1;
        }
        /* edge cases */
        if (zcsr_sort_instances(a, 0, scratch, sizeof(scratch)) != 0) {
            printf("FAIL: sort count=0 should return 0\n"); return 1;
        }
        if (zcsr_sort_instances(a, 1, scratch, sizeof(scratch)) != 1) {
            printf("FAIL: sort count=1 should return 1\n"); return 1;
        }
        printf("PASS: instanced radix sort correctness (%d items, %zu runs, stable)\n", N, runs);
    }

    zcsr_surface* surface = zcsr_surface_create("zcsr glrender smoke", (zcsr_rect){ 0, 0, 320, 240 });
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
    zcsr_gl_texture tex = zcsr_gl_texture_create(renderer, rgba, 2, 2);
    if (!tex) {
        printf("FAIL: texture upload failed\n");
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(surface);
        return 1;
    }
    if (!zcsr_gl_texture_upload(renderer, tex, rgba, 2, 2)) {
        printf("FAIL: texture re-upload failed\n");
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(surface);
        return 1;
    }

    zcsr_gl_texture_filter(renderer, tex, true);
    zcsr_gl_texture_filter(renderer, tex, false);
    zcsr_gl_sprite sprite = { tex, { 16.0f, 16.0f, 64.0f, 64.0f }, 0.25f, 1.0f, 1.0f, 1.0f, 1.0f,
                              ZCSR_MOD_MULTIPLY, 0.0f };
    zcsr_gl_begin(renderer);
    zcsr_gl_submit(renderer, &sprite);
    zcsr_gl_draw_text(renderer, "ZCSR", 4.0f, 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    zcsr_gl_flush(renderer);
    zcsr_gl_texture_destroy(renderer, tex);
    zcsr_gl_destroy(renderer);
    zcsr_surface_destroy(surface);
    printf("PASS: GL renderer smoke\n");
    return 0;
}
