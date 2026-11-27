#include "zcsr/gl_render.h"
#include "zcsr/render_tier.h"
#include "zcsr/gl_render_light2d_ext.h"
#include "zcsr/gl_render_l2d_geo.h"
#include "zcsr/camera.h"
#include "zcsr/color_pipeline.h"
#include "zcsr/scene.h"
#include "zcsr/scene_submit.h"

#include <stdio.h>
#include <string.h>

static float encode_identity(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float decode_identity(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static zcsr_scene_entity make_scene_entity(float x, float y, float z, float h,
                                           uint32_t kind) {
    zcsr_scene_entity e;
    memset(&e, 0, sizeof(e));
    e.x = x;
    e.y = y;
    e.z = z;
    e.w = 2.0f;
    e.h = h;
    e.pivot_x = 0.5f;
    e.pivot_y = 0.0f;
    e.layer = 0;
    e.order = 0;
    e.tint_r = e.tint_g = e.tint_b = 1.0f;
    e.tint_a = 1.0f;
    e.visible = true;
    e.kind = kind;
    return e;
}

int main(void) {
    static unsigned char renderer_buf[16 * 1024 * 1024];
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
    zcsr_gl_submit_quad(0, 0);
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
    if (zcsr_gl_texture_create_ex(0, rgba, 2, 2, ZCSR_COLOR_SPACE_SRGB, false) != 0) {
        printf("FAIL: texture_create_ex(NULL renderer) must return 0\n");
        return 1;
    }
    if (zcsr_gl_texture_query_meta(0, 0, 0, 0)) {
        printf("FAIL: texture_query_meta(NULL renderer) must return false\n");
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
#if ZCSR_SORT_TIER == ZCSR_SORT_TIER_NONE
        /* NONE tier: sort stage compiled out, identity order; submission order
         * preserved, runs counted over the original seed sequence. */
        size_t runs = zcsr_sort_instances(a, N, scratch, sizeof(scratch));
        int ok = 1;
        for (int i = 0; i < N; ++i) {
            if (a[i].tex != seeds[i] || a[i].cx != (float)i) { ok = 0; break; }
        }
        if (!ok) {
            printf("FAIL: NONE tier must keep identity submission order\n");
            return 1;
        }
        size_t expected_runs = 1;
        for (int i = 1; i < N; ++i) if (seeds[i] != seeds[i-1]) ++expected_runs;
#else
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
#endif
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

    if (!zcsr_render_backend_available(ZCSR_RENDER_BACKEND_GPU)
        || zcsr_render_backend_available(ZCSR_RENDER_BACKEND_CPU)) {
        printf("FAIL: backend availability must be GPU-only in 1.0\n");
        return 1;
    }
    zcsr_gl_renderer* renderer = zcsr_gl_create_with_backend(
        surface, renderer_buf, sizeof renderer_buf, ZCSR_RENDER_BACKEND_CPU);
    if (renderer) {
        printf("FAIL: CPU backend must not create a second runtime\n");
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(surface);
        return 1;
    }
    renderer = zcsr_gl_create_with_backend(
        surface, renderer_buf, sizeof renderer_buf, ZCSR_RENDER_BACKEND_GPU);
    if (!renderer) {
        printf("SKIP: GL renderer unavailable on this surface/backend\n");
        zcsr_surface_destroy(surface);
        return 0;
    }

    zcsr_gl_set_vsync(renderer, false);

    /* ---- Light2D target contract + LIT binding precondition ---- */
    {
        static const zcsr_gl_l2d_format bad_count[3] = {
            ZCSR_GL_L2D_FORMAT_RGBA16F,
            ZCSR_GL_L2D_FORMAT_RGBA16F,
            ZCSR_GL_L2D_FORMAT_RGBA8
        };
        static const zcsr_gl_l2d_format bad_albedo[4] = {
            ZCSR_GL_L2D_FORMAT_RGBA8,
            ZCSR_GL_L2D_FORMAT_RGBA16F,
            ZCSR_GL_L2D_FORMAT_RGBA8,
            ZCSR_GL_L2D_FORMAT_RGBA16F
        };
        static const zcsr_gl_l2d_format bad_normal[4] = {
            ZCSR_GL_L2D_FORMAT_RGBA16F,
            ZCSR_GL_L2D_FORMAT_RGBA16F,
            ZCSR_GL_L2D_FORMAT_SRGB8_ALPHA8,
            ZCSR_GL_L2D_FORMAT_RGBA16F
        };
        static const zcsr_gl_l2d_format good[4] = {
            ZCSR_GL_L2D_FORMAT_RGBA16F,
            ZCSR_GL_L2D_FORMAT_RGBA16F,
            ZCSR_GL_L2D_FORMAT_RGBA8,
            ZCSR_GL_L2D_FORMAT_RGBA16F
        };
        zcsr_gl_l2d_target target = ZCSR_GL_L2D_TARGET_INVALID;
        zcsr_camera camera = {
            0.0f, 0.0f, 1.0f, 320, 240, 1.0f
        };
        if (zcsr_gl_l2d_target_create(renderer, 320, 240, bad_count, 3,
                                      &target) ||
            target != ZCSR_GL_L2D_TARGET_INVALID) {
            printf("FAIL: target_create accepted 3 attachments\n");
            return 1;
        }
        if (zcsr_gl_l2d_target_create(renderer, 320, 240, bad_albedo, 4,
                                      &target)) {
            printf("FAIL: target_create accepted RGBA8 albedo\n");
            return 1;
        }
        if (zcsr_gl_l2d_target_create(renderer, 320, 240, bad_normal, 4,
                                      &target)) {
            printf("FAIL: target_create accepted SRGB8 normal\n");
            return 1;
        }
        if (!zcsr_gl_l2d_target_create(renderer, 320, 240, good, 4, &target)) {
            printf("FAIL: target_create rejected the exact G-buffer contract\n");
            return 1;
        }
        if (zcsr_gl_l2d_gbuffer_is_bound(renderer)) {
            printf("FAIL: gbuffer_is_bound true before gbuffer_begin\n");
            return 1;
        }
        if (zcsr_gl_l2d_geo_begin(renderer, &camera)) {
            printf("FAIL: geo_begin accepted a missing G-buffer\n");
            return 1;
        }
        if (!zcsr_gl_l2d_gbuffer_begin(renderer, target, 320, 240) ||
            !zcsr_gl_l2d_gbuffer_is_bound(renderer)) {
            printf("FAIL: gbuffer_begin did not establish the bound contract\n");
            return 1;
        }
        if (!zcsr_gl_l2d_geo_begin(renderer, &camera)) {
            printf("FAIL: geo_begin rejected a valid bound G-buffer\n");
            return 1;
        }
        if (!zcsr_gl_l2d_geo_end(renderer)) {
            printf("FAIL: geo_end after valid LIT begin\n");
            return 1;
        }

        /* ---- real zcsr_scene_submit, both paths ---- */
        {
            zcsr_scene scene;
            zcsr_scene_entity far_e, raised_e, near_e, light_e;
            zcsr_scene_quad_instance scratch[3];
            zcsr_scene_submit_stats stats;

            zcsr_scene_init(&scene);
            zcsr_scene_camera_init(&scene.camera, 320, 240, 0.0f, 45.0f, 0.0f,
                                   20.0f, 5.0f);
            far_e = make_scene_entity(0.0f, 0.0f, -6.0f, 2.0f,
                                      ZCSR_SCENE_KIND_RECT);
            raised_e = make_scene_entity(0.0f, 0.0f, -6.0f, 6.0f,
                                         ZCSR_SCENE_KIND_RECT);
            near_e = make_scene_entity(0.0f, 0.0f, 3.0f, 2.0f,
                                       ZCSR_SCENE_KIND_RECT);
            light_e = make_scene_entity(0.0f, 0.0f, 0.0f, 2.0f,
                                        ZCSR_SCENE_KIND_LIGHT);
            if (zcsr_scene_add(&scene, &far_e) == 0 ||
                zcsr_scene_add(&scene, &raised_e) == 0 ||
                zcsr_scene_add(&scene, &near_e) == 0 ||
                zcsr_scene_add(&scene, &light_e) == 0) {
                printf("FAIL: scene_submit fixture could not add entities\n");
                return 1;
            }

            memset(&stats, 0, sizeof(stats));
            if (!zcsr_scene_submit(renderer, &scene, ZCSR_SCENE_SUBMIT_LIT,
                                   NULL, 0, &stats)) {
                printf("FAIL: LIT scene_submit failed with a bound G-buffer\n");
                return 1;
            }
            if (stats.visible != 4 || stats.drawable != 3 ||
                stats.submitted != 3 || stats.skipped != 1) {
                printf("FAIL: LIT stats visible=%zu drawable=%zu submitted=%zu skipped=%zu\n",
                       stats.visible, stats.drawable, stats.submitted,
                       stats.skipped);
                return 1;
            }

            if (!zcsr_gl_l2d_bind_default_target(renderer, 320, 240) ||
                zcsr_gl_l2d_gbuffer_is_bound(renderer)) {
                printf("FAIL: default target still reports G-buffer bound\n");
                return 1;
            }

            memset(&stats, 0, sizeof(stats));
            if (!zcsr_scene_submit(renderer, &scene, ZCSR_SCENE_SUBMIT_DISPLAY,
                                   scratch, 3, &stats)) {
                printf("FAIL: DISPLAY scene_submit failed at exact capacity\n");
                return 1;
            }
            if (stats.visible != 4 || stats.drawable != 3 ||
                stats.submitted != 3 || stats.skipped != 1) {
                printf("FAIL: DISPLAY stats visible=%zu drawable=%zu submitted=%zu skipped=%zu\n",
                       stats.visible, stats.drawable, stats.submitted,
                       stats.skipped);
                return 1;
            }

            memset(&stats, 0, sizeof(stats));
            if (zcsr_scene_submit(renderer, &scene, ZCSR_SCENE_SUBMIT_DISPLAY,
                                  scratch, 2, &stats)) {
                printf("FAIL: DISPLAY scene_submit accepted short capacity\n");
                return 1;
            }
            if (stats.drawable != 3 || stats.submitted != 0) {
                printf("FAIL: short-capacity stats drawable=%zu submitted=%zu\n",
                       stats.drawable, stats.submitted);
                return 1;
            }

            memset(&stats, 0, sizeof(stats));
            if (zcsr_scene_submit(renderer, &scene, ZCSR_SCENE_SUBMIT_LIT,
                                   NULL, 0, &stats)) {
                printf("FAIL: LIT scene_submit accepted a missing G-buffer\n");
                return 1;
            }
            if (stats.visible != 4 || stats.drawable != 3 ||
                stats.submitted != 0) {
                printf("FAIL: unbound LIT stats visible=%zu drawable=%zu submitted=%zu\n",
                       stats.visible, stats.drawable, stats.submitted);
                return 1;
            }
        }

        zcsr_gl_l2d_target_destroy(renderer, target);
    }

    /* ---- color pipeline: output replaceable, input decode fixed sRGB ---- */
    {
        const zcsr_color_pipeline_desc* srgb = zcsr_color_pipeline_default();
        static zcsr_color_pipeline_desc custom;
        zcsr_color_pipeline_desc bad = {
            "test.identity-both", encode_identity, decode_identity
        };
        if (srgb == NULL) {
            printf("FAIL: default color pipeline missing\n");
            return 1;
        }
        custom.stable_key = "test.identity-output";
        custom.to_display = encode_identity;
        custom.to_linear = srgb->to_linear;
        if (!zcsr_gl_l2d_set_color_pipeline(renderer, &custom)) {
            printf("FAIL: rejected output-only custom color pipeline\n");
            return 1;
        }
        if (zcsr_gl_l2d_get_color_pipeline(renderer) != &custom) {
            printf("FAIL: custom color pipeline was not retained\n");
            return 1;
        }
        if (zcsr_gl_l2d_set_color_pipeline(renderer, &bad)) {
            printf("FAIL: accepted a non-sRGB geometry decode\n");
            return 1;
        }
        if (zcsr_gl_l2d_get_color_pipeline(renderer) != &custom) {
            printf("FAIL: rejected pipeline replaced the active custom one\n");
            return 1;
        }
    }

    zcsr_gl_texture tex = zcsr_gl_texture_create(renderer, rgba, 2, 2);
    if (!tex) {
        printf("FAIL: texture upload failed\n");
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(surface);
        return 1;
    }
    {
        zcsr_texture_color_space cs = ZCSR_COLOR_SPACE_LINEAR;
        bool has_alpha = false;
        if (!zcsr_gl_texture_query_meta(renderer, tex, &cs, &has_alpha) ||
            cs != ZCSR_COLOR_SPACE_DISPLAY || !has_alpha) {
            printf("FAIL: default texture metadata is not DISPLAY/alpha\n");
            zcsr_gl_texture_destroy(renderer, tex);
            zcsr_gl_destroy(renderer);
            zcsr_surface_destroy(surface);
            return 1;
        }
    }
    {
        zcsr_gl_texture srgb = zcsr_gl_texture_create_ex(renderer, rgba, 2, 2,
                                                         ZCSR_COLOR_SPACE_SRGB, false);
        zcsr_texture_color_space cs = ZCSR_COLOR_SPACE_DISPLAY;
        bool has_alpha = true;
        if (!srgb || !zcsr_gl_texture_query_meta(renderer, srgb, &cs, &has_alpha) ||
            cs != ZCSR_COLOR_SPACE_SRGB || has_alpha) {
            printf("FAIL: SRGB texture metadata mismatch\n");
            if (srgb) zcsr_gl_texture_destroy(renderer, srgb);
            zcsr_gl_texture_destroy(renderer, tex);
            zcsr_gl_destroy(renderer);
            zcsr_surface_destroy(surface);
            return 1;
        }
        zcsr_gl_texture_destroy(renderer, srgb);
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
                              ZCSR_MOD_MULTIPLY, 0.0f, false, false };
    zcsr_gl_begin(renderer);
    zcsr_gl_submit(renderer, &sprite);
    {
        zcsr_gl_quad quad = {
            tex,
            { 120.0f, 200.0f, 180.0f, 120.0f },
            { 40.0f, 40.0f, 130.0f, 90.0f },
            { 0.0f, 0.0f, 1.0f, 0.0f },
            { 1.0f, 1.0f, 0.0f, 1.0f },
            1.0f, 1.0f, 1.0f, 1.0f,
            ZCSR_MOD_MULTIPLY, 0.0f
        };
        if (!zcsr_gl_submit_quad(renderer, &quad)) {
            printf("FAIL: arbitrary quad submit rejected\n");
            zcsr_gl_texture_destroy(renderer, tex);
            zcsr_gl_destroy(renderer);
            zcsr_surface_destroy(surface);
            return 1;
        }
    }
    zcsr_gl_draw_text(renderer, "ZCSR", 4.0f, 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    zcsr_gl_flush(renderer);
    zcsr_gl_present(renderer);

    /* ---- unified linear frame: one decode at the texture boundary and one
     * encode at present. A 0.5 display gray must round-trip, and 50% black
     * over white must blend in linear light (== 188 display), not 128.
     * LINEAR_HDR-only: the DIRECT_SRGB slim tier writes the default frame
     * directly and has no encode pass, so these round-trip numbers do not
     * apply. Compiled out under ZCSR_COLOR_OPT_DIRECT_SRGB. ---- */
#if ZCSR_COLOR_TIER == ZCSR_COLOR_OPT_LINEAR_HDR
    {
        unsigned char gray = 128;
        unsigned char white_px[4] = { 255, 255, 255, 255 };
        static unsigned char px[1920 * 1080 * 4];
        zcsr_gl_texture gray_tex;
        zcsr_gl_texture white_tex;
        zcsr_gl_sprite s;
        int x, y, off;

        if (!zcsr_gl_l2d_set_color_pipeline(renderer, zcsr_color_pipeline_default())) {
            printf("FAIL: could not restore default color pipeline\n");
            return 1;
        }
        gray_tex = zcsr_gl_texture_create(renderer,
            (const unsigned char[]){ gray, gray, gray, 255 }, 1, 1);
        white_tex = zcsr_gl_texture_create(renderer, white_px, 1, 1);
        if (!gray_tex || !white_tex) {
            printf("FAIL: linear-frame texture fixture creation failed\n");
            return 1;
        }

        zcsr_gl_clear(renderer, 0.0f, 0.0f, 0.0f, 1.0f);
        zcsr_gl_begin(renderer);
        s = (zcsr_gl_sprite){ gray_tex, { 0.0f, 0.0f, 1920.0f, 1080.0f }, 0.0f,
                              1.0f, 1.0f, 1.0f, 1.0f,
                              ZCSR_MOD_MULTIPLY, 0.0f, false, false };
        zcsr_gl_submit(renderer, &s);
        zcsr_gl_flush(renderer);
        if (!zcsr_gl_read_rgba(renderer, px, sizeof(px), &x, &y)) {
            printf("FAIL: linear-frame readback failed\n");
            return 1;
        }
        off = ((y / 2) * x + x / 2) * 4;
        if (px[off] < 126u || px[off] > 130u) {
            printf("FAIL: display gray 128 round-trip got %u\n", (unsigned)px[off]);
            return 1;
        }

        zcsr_gl_clear(renderer, 1.0f, 1.0f, 1.0f, 1.0f);
        zcsr_gl_begin(renderer);
        s = (zcsr_gl_sprite){ white_tex, { 0.0f, 0.0f, 1920.0f, 1080.0f }, 0.0f,
                              0.0f, 0.0f, 0.0f, 0.5f,
                              ZCSR_MOD_MULTIPLY, 0.0f, false, false };
        zcsr_gl_submit(renderer, &s);
        zcsr_gl_flush(renderer);
        if (!zcsr_gl_read_rgba(renderer, px, sizeof(px), &x, &y)) {
            printf("FAIL: linear-frame blend readback failed\n");
            return 1;
        }
        off = ((y / 2) * x + x / 2) * 4;
        if (px[off] < 182u || px[off] > 194u) {
            printf("FAIL: 50%% black over white should encode near 188, got %u\n",
                   (unsigned)px[off]);
            return 1;
        }
        zcsr_gl_texture_destroy(renderer, gray_tex);
        zcsr_gl_texture_destroy(renderer, white_tex);
        printf("PASS: unified linear frame round-trip and linear blending\n");
    }
#endif

    /* ---- selective hue remap and shared ramp atlas: both algorithms run in
     * the linear frame, after tint, and leave the legacy path untouched when
     * their amounts are zero. LINEAR_HDR-only: the DIRECT_SRGB slim fragment
     * compiles no hue/ramp/glow/chroma/emiss/rim stage and rejects such an
     * instance at the selection gate instead of drawing it untransformed. ---- */
#if ZCSR_COLOR_TIER == ZCSR_COLOR_OPT_LINEAR_HDR
    {
        unsigned char green_px[4] = { 0, 255, 0, 255 };
        unsigned char ramp_px[2][2][4] = {
            { { 0, 0, 0, 255 }, { 255, 0, 0, 255 } },   /* row 0: black -> red */
            { { 0, 0, 0, 255 }, { 0, 0, 255, 255 } }    /* row 1: black -> blue */
        };
        static unsigned char px2[1920 * 1080 * 4];
        zcsr_gl_texture green_tex = zcsr_gl_texture_create(renderer, green_px, 1, 1);
        zcsr_gl_texture ramp_tex = zcsr_gl_texture_create(renderer, &ramp_px[0][0][0], 2, 2);
        zcsr_gl_texture white_tex = zcsr_gl_texture_create(renderer,
            (const unsigned char[]){ 255, 255, 255, 255 }, 1, 1);
        zcsr_gl_texture black_tex = zcsr_gl_texture_create(renderer,
            (const unsigned char[]){ 0, 0, 0, 255 }, 1, 1);
        zcsr_gl_texture lime_tex = zcsr_gl_texture_create(renderer,
            (const unsigned char[]){ 180, 255, 0, 255 }, 1, 1);
        zcsr_gl_texture gray_tex = zcsr_gl_texture_create(renderer,
            (const unsigned char[]){ 120, 110, 150, 255 }, 1, 1);
        zcsr_gl_texture stem_tex = zcsr_gl_texture_create(renderer,
            (const unsigned char[]){ 120, 180, 20, 255 }, 1, 1);
        zcsr_gl_texture dark_green_tex = zcsr_gl_texture_create(renderer,
            (const unsigned char[]){ 20, 70, 20, 255 }, 1, 1);
        zcsr_gl_texture bright_green_tex = zcsr_gl_texture_create(renderer,
            (const unsigned char[]){ 190, 255, 190, 255 }, 1, 1);
        zcsr_gl_texture yellow_tex = zcsr_gl_texture_create(renderer,
            (const unsigned char[]){ 255, 255, 0, 255 }, 1, 1);
        zcsr_gl_instance inst;
        int x2, y2, off2;
        if (!green_tex || !ramp_tex || !white_tex || !black_tex || !lime_tex || !gray_tex || !stem_tex ||
            !dark_green_tex || !bright_green_tex || !yellow_tex) {
            printf("FAIL: recolor fixture creation failed\n");
            return 1;
        }
        zcsr_gl_set_color_ramp(renderer, ramp_tex, 2);

        /* green (hue 120) -> red (hue 0); white/black are chroma-neutral and
         * would be left alone by the 0.02 saturation gate. */
        zcsr_gl_clear(renderer, 0.0f, 0.0f, 0.0f, 1.0f);
        zcsr_gl_begin(renderer);
        inst = (zcsr_gl_instance){
            .cx = 960.0f, .cy = 540.0f, .w = 1920.0f, .h = 1080.0f,
            .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .mode = (float)ZCSR_MOD_MULTIPLY,
            .recolor = { .hue_source = 120.0f, .hue_target = 0.0f,
                         .hue_width = 40.0f, .hue_amount = 1.0f,
                         .ramp_row = 0.0f, .ramp_amount = 0.0f },
            .tex = green_tex, .z = 0.0f, .has_alpha = false
        };
        if (zcsr_gl_draw_instances(renderer, green_tex, &inst, 1) != 1) {
            printf("FAIL: hue remap instance draw failed\n");
            return 1;
        }
        if (!zcsr_gl_read_rgba(renderer, px2, sizeof(px2), &x2, &y2)) {
            printf("FAIL: hue remap readback failed\n");
            return 1;
        }
        off2 = ((y2 / 2) * x2 + x2 / 2) * 4;
        if (px2[off2 + 0] < 220u || px2[off2 + 1] > 40u || px2[off2 + 2] > 40u) {
            printf("FAIL: green -> red hue remap got (%u,%u,%u)\n",
                   (unsigned)px2[off2 + 0], (unsigned)px2[off2 + 1], (unsigned)px2[off2 + 2]);
            return 1;
        }

        /* yellow-green (a normal highlight hue) must compress toward red, not
         * land on magenta from a fixed -120 degree shift. */
        zcsr_gl_clear(renderer, 0.0f, 0.0f, 0.0f, 1.0f);
        zcsr_gl_begin(renderer);
        inst = (zcsr_gl_instance){
            .cx = 960.0f, .cy = 540.0f, .w = 1920.0f, .h = 1080.0f,
            .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .mode = (float)ZCSR_MOD_MULTIPLY,
            .recolor = { .hue_source = 120.0f, .hue_target = 0.0f,
                         .hue_width = 100.0f, .hue_amount = 1.0f,
                         .ramp_row = 0.0f, .ramp_amount = 0.0f },
            .tex = lime_tex, .z = 0.0f, .has_alpha = false
        };
        if (zcsr_gl_draw_instances(renderer, lime_tex, &inst, 1) != 1) {
            printf("FAIL: yellow-green hue instance draw failed\n");
            return 1;
        }
        if (!zcsr_gl_read_rgba(renderer, px2, sizeof(px2), &x2, &y2)) {
            printf("FAIL: yellow-green hue readback failed\n");
            return 1;
        }
        off2 = ((y2 / 2) * x2 + x2 / 2) * 4;
        if (px2[off2 + 0] < 200u || px2[off2 + 2] > 80u) {
            printf("FAIL: yellow-green -> red got (%u,%u,%u)\n",
                   (unsigned)px2[off2 + 0], (unsigned)px2[off2 + 1], (unsigned)px2[off2 + 2]);
            return 1;
        }

        /* white luma -> row 1 blue ramp */
        zcsr_gl_clear(renderer, 0.0f, 0.0f, 0.0f, 1.0f);
        zcsr_gl_begin(renderer);
        inst = (zcsr_gl_instance){
            .cx = 960.0f, .cy = 540.0f, .w = 1920.0f, .h = 1080.0f,
            .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .mode = (float)ZCSR_MOD_MULTIPLY,
            .recolor = { .hue_source = 0.0f, .hue_target = 0.0f,
                         .hue_width = 1.0f, .hue_amount = 0.0f,
                         .ramp_row = 1.0f, .ramp_amount = 1.0f },
            .tex = white_tex, .z = 0.0f, .has_alpha = false
        };
        if (zcsr_gl_draw_instances(renderer, white_tex, &inst, 1) != 1) {
            printf("FAIL: ramp instance draw failed\n");
            return 1;
        }
        if (!zcsr_gl_read_rgba(renderer, px2, sizeof(px2), &x2, &y2)) {
            printf("FAIL: ramp readback failed\n");
            return 1;
        }
        off2 = ((y2 / 2) * x2 + x2 / 2) * 4;
        if (px2[off2 + 2] < 220u || px2[off2 + 0] > 40u || px2[off2 + 1] > 40u) {
            printf("FAIL: white -> blue ramp got (%u,%u,%u)\n",
                   (unsigned)px2[off2 + 0], (unsigned)px2[off2 + 1], (unsigned)px2[off2 + 2]);
            return 1;
        }

        /* low-saturation blue-gray -> saturated cyan: hue rotation alone
         * preserves S, so this is the fume-smoke correction stage. */
        zcsr_gl_clear(renderer, 0.0f, 0.0f, 0.0f, 1.0f);
        zcsr_gl_begin(renderer);
        inst = (zcsr_gl_instance){
            .cx = 960.0f, .cy = 540.0f, .w = 1920.0f, .h = 1080.0f,
            .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .mode = (float)ZCSR_MOD_MULTIPLY,
            .recolor = { .hue_source = 280.0f, .hue_target = 195.0f,
                         .hue_width = 42.0f, .hue_amount = 1.0f,
                         .sat_boost = 0.62f, .sat_floor = 0.30f,
                         .value_gain = 0.25f, .value_contrast = 0.10f },
            .tex = gray_tex, .z = 0.0f, .has_alpha = false
        };
        if (zcsr_gl_draw_instances(renderer, gray_tex, &inst, 1) != 1) {
            printf("FAIL: chroma conditioning instance draw failed\n");
            return 1;
        }
        if (!zcsr_gl_read_rgba(renderer, px2, sizeof(px2), &x2, &y2)) {
            printf("FAIL: chroma conditioning readback failed\n");
            return 1;
        }
        off2 = ((y2 / 2) * x2 + x2 / 2) * 4;
        if (px2[off2 + 2] < 130u || (int)px2[off2 + 2] - (int)px2[off2 + 0] < 60) {
            printf("FAIL: gray-blue -> saturated cyan got (%u,%u,%u)\n",
                   (unsigned)px2[off2 + 0], (unsigned)px2[off2 + 1], (unsigned)px2[off2 + 2]);
            return 1;
        }

        /* yellow-green stem outside the purple source family must not take
         * the ice ramp; ramp weight is gated by the hue-family hit mask. */
        zcsr_gl_clear(renderer, 0.0f, 0.0f, 0.0f, 1.0f);
        zcsr_gl_begin(renderer);
        inst = (zcsr_gl_instance){
            .cx = 960.0f, .cy = 540.0f, .w = 1920.0f, .h = 1080.0f,
            .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .mode = (float)ZCSR_MOD_MULTIPLY,
            .recolor = { .hue_source = 280.0f, .hue_target = 195.0f,
                         .hue_width = 42.0f, .hue_amount = 1.0f,
                         .ramp_row = 1.0f, .ramp_amount = 1.0f },
            .tex = stem_tex, .z = 0.0f, .has_alpha = false
        };
        if (zcsr_gl_draw_instances(renderer, stem_tex, &inst, 1) != 1) {
            printf("FAIL: masked ramp instance draw failed\n");
            return 1;
        }
        if (!zcsr_gl_read_rgba(renderer, px2, sizeof(px2), &x2, &y2)) {
            printf("FAIL: masked ramp readback failed\n");
            return 1;
        }
        off2 = ((y2 / 2) * x2 + x2 / 2) * 4;
        if (px2[off2 + 1] < 140u || (int)px2[off2 + 1] - (int)px2[off2 + 0] < 20 ||
            (int)px2[off2 + 1] - (int)px2[off2 + 2] < 20) {
            printf("FAIL: ice ramp leaked into non-purple stem got (%u,%u,%u)\n",
                   (unsigned)px2[off2 + 0], (unsigned)px2[off2 + 1], (unsigned)px2[off2 + 2]);
            return 1;
        }

        /* OKLab chroma ramp must preserve source luminance: dark and bright
         * greens stay separated after the same blue material gradient. */
        zcsr_gl_clear(renderer, 0.0f, 0.0f, 0.0f, 1.0f);
        zcsr_gl_begin(renderer);
        inst = (zcsr_gl_instance){
            .cx = 960.0f, .cy = 540.0f, .w = 1920.0f, .h = 1080.0f,
            .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .mode = (float)ZCSR_MOD_MULTIPLY,
            .recolor = { .hue_amount = 0.0f,
                         .ramp_row = 1.0f, .ramp_amount = 1.0f,
                         .ramp_mode = 1.0f, .ramp_luma = 0.0f },
            .tex = dark_green_tex, .z = 0.0f, .has_alpha = false
        };
        if (zcsr_gl_draw_instances(renderer, dark_green_tex, &inst, 1) != 1) {
            printf("FAIL: dark chroma-ramp draw failed\n");
            return 1;
        }
        if (!zcsr_gl_read_rgba(renderer, px2, sizeof(px2), &x2, &y2)) {
            printf("FAIL: dark chroma-ramp readback failed\n");
            return 1;
        }
        off2 = ((y2 / 2) * x2 + x2 / 2) * 4;
        int dark_luma = (int)(0.2126 * px2[off2] + 0.7152 * px2[off2 + 1] +
                              0.0722 * px2[off2 + 2]);

        zcsr_gl_clear(renderer, 0.0f, 0.0f, 0.0f, 1.0f);
        zcsr_gl_begin(renderer);
        inst.tex = bright_green_tex;
        if (zcsr_gl_draw_instances(renderer, bright_green_tex, &inst, 1) != 1) {
            printf("FAIL: bright chroma-ramp draw failed\n");
            return 1;
        }
        if (!zcsr_gl_read_rgba(renderer, px2, sizeof(px2), &x2, &y2)) {
            printf("FAIL: bright chroma-ramp readback failed\n");
            return 1;
        }
        off2 = ((y2 / 2) * x2 + x2 / 2) * 4;
        int bright_luma = (int)(0.2126 * px2[off2] + 0.7152 * px2[off2 + 1] +
                                0.0722 * px2[off2 + 2]);
        if (bright_luma < dark_luma + 80) {
            printf("FAIL: chroma ramp flattened source gradient dark=%d bright=%d\n",
                   dark_luma, bright_luma);
            return 1;
        }

        /* HDR core + one final tonemap: white emissive on a yellow sprite must
         * roll the blue channel off instead of hard-clipping to white. */
        zcsr_gl_clear(renderer, 0.0f, 0.0f, 0.0f, 1.0f);
        zcsr_gl_begin(renderer);
        inst = (zcsr_gl_instance){
            .cx = 960.0f, .cy = 540.0f, .w = 1920.0f, .h = 1080.0f,
            .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .mode = (float)ZCSR_MOD_MULTIPLY,
            .recolor = { .emiss_r = 1.0f, .emiss_g = 1.0f, .emiss_b = 1.0f,
                         .emiss_amount = 2.0f },
            .tex = yellow_tex, .z = 0.0f, .has_alpha = false
        };
        if (zcsr_gl_draw_instances(renderer, yellow_tex, &inst, 1) != 1) {
            printf("FAIL: HDR emissive draw failed\n");
            return 1;
        }
        if (!zcsr_gl_read_rgba(renderer, px2, sizeof(px2), &x2, &y2)) {
            printf("FAIL: HDR emissive readback failed\n");
            return 1;
        }
        off2 = ((y2 / 2) * x2 + x2 / 2) * 4;
        if (px2[off2 + 0] < 240u || px2[off2 + 1] < 240u || px2[off2 + 2] > 235u) {
            printf("FAIL: HDR tonemap did not roll off blue got (%u,%u,%u)\n",
                   (unsigned)px2[off2 + 0], (unsigned)px2[off2 + 1], (unsigned)px2[off2 + 2]);
            return 1;
        }


        zcsr_gl_clear(renderer, 0.0f, 0.0f, 0.0f, 1.0f);
        zcsr_gl_begin(renderer);
        inst = (zcsr_gl_instance){
            .cx = 960.0f, .cy = 540.0f, .w = 1920.0f, .h = 1080.0f,
            .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .mode = (float)ZCSR_MOD_MULTIPLY,
            .recolor = { .hue_amount = 0.0f, .ramp_amount = 0.0f,
                         .glow_r = 0.0f, .glow_g = 0.0f, .glow_b = 1.0f,
                         .glow_amount = 1.0f },
            .tex = black_tex, .z = 0.0f, .has_alpha = false
        };
        if (zcsr_gl_draw_instances(renderer, black_tex, &inst, 1) != 1) {
            printf("FAIL: glow instance draw failed\n");
            return 1;
        }
        if (!zcsr_gl_read_rgba(renderer, px2, sizeof(px2), &x2, &y2)) {
            printf("FAIL: glow readback failed\n");
            return 1;
        }
        off2 = ((y2 / 2) * x2 + x2 / 2) * 4;
        if (px2[off2 + 2] < 220u || px2[off2 + 0] > 40u || px2[off2 + 1] > 40u) {
            printf("FAIL: black + blue glow got (%u,%u,%u)\n",
                   (unsigned)px2[off2 + 0], (unsigned)px2[off2 + 1], (unsigned)px2[off2 + 2]);
            return 1;
        }
        zcsr_gl_set_color_ramp(renderer, 0, 0);
        zcsr_gl_texture_destroy(renderer, green_tex);
        zcsr_gl_texture_destroy(renderer, ramp_tex);
        zcsr_gl_texture_destroy(renderer, white_tex);
        zcsr_gl_texture_destroy(renderer, black_tex);
        zcsr_gl_texture_destroy(renderer, lime_tex);
        zcsr_gl_texture_destroy(renderer, gray_tex);
        zcsr_gl_texture_destroy(renderer, stem_tex);
        zcsr_gl_texture_destroy(renderer, dark_green_tex);
        zcsr_gl_texture_destroy(renderer, bright_green_tex);
        zcsr_gl_texture_destroy(renderer, yellow_tex);
        printf("PASS: selective hue remap, shared ramp atlas, additive glow, chroma conditioning\n");
    }
#endif

    zcsr_gl_texture_destroy(renderer, tex);
    zcsr_gl_destroy(renderer);
    zcsr_surface_destroy(surface);
    printf("PASS: GL renderer smoke\n");
    return 0;
}
