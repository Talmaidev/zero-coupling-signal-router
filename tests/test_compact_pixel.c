/* test_compact_pixel.c — minimal offscreen contract for the COMPACT 36 B tier.
 *
 * Renders one rotated, semi-transparent sprite through the instanced path and
 * verifies:
 *   1. the instance is accepted and submitted (plain white multiply; the 36 B
 *      record carries packed rotation + 8-bit alpha only);
 *   2. the covered center pixel is green-dominant (modulation + alpha blend);
 *   3. the rotation actually expanded the quad: a point outside the axis-aligned
 *      100 px box but inside the 30° rotated bounding box is non-white.
 *
 * COMPACT tier calls zcsr_gl_draw_instances_compact with a caller-packed 36 B
 * zcsr_instance_compact record (no 168 B record, no conversion on the path).
 * FULL tier keeps the 168 B zcsr_gl_instance record through the generic entry,
 * so the two snapshots render the same fixture for pixel comparison.
 *
 * When argv[1] is a writable path the readback RGBA8 snapshot is dumped in
 * the ZCSRPIX1 container for cross-build pixel comparison. The optional
 * argv[2]="45" selects the exact rot16=8192 (45°) preset for that comparison;
 * ctest runs without arguments and keeps the 30° fixture.
 *
 * The tier assembly printed in the PASS line comes from the compile-time
 * macros the build configured; there is no runtime tier lookup.
 */
#include "zcsr/gl_render.h"
#include "zcsr/render_tier.h"

#include <stdio.h>
#include <string.h>

#define CP_W 1920
#define CP_H 1080

#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
#define CP_LAYOUT_NAME "compact"
#else
#define CP_LAYOUT_NAME "full"
#endif

int main(int argc, char** argv) {
    static unsigned char renderer_buf[16 * 1024 * 1024];
    static unsigned char px[CP_W * CP_H * 4];

    zcsr_surface* surface = zcsr_surface_create("zcsr compact pixel",
                                                 (zcsr_rect){ 0, 0, 1920, 1080 });
    if (!surface) {
        printf("SKIP: no window surface (headless / no DISPLAY)\n");
        return 0;
    }

    if (!zcsr_render_backend_available(ZCSR_RENDER_BACKEND_GPU)
        || zcsr_render_backend_available(ZCSR_RENDER_BACKEND_CPU)) {
        printf("FAIL: backend availability must be GPU-only in 1.0\n");
        zcsr_surface_destroy(surface);
        return 1;
    }

    zcsr_gl_renderer* renderer = zcsr_gl_create_with_backend(
        surface, renderer_buf, sizeof renderer_buf, ZCSR_RENDER_BACKEND_GPU);
    if (!renderer) {
        printf("SKIP: GL renderer unavailable on this surface/backend\n");
        zcsr_surface_destroy(surface);
        return 0;
    }
    zcsr_gl_set_vsync(renderer, false);

    static const unsigned char green[4] = { 0, 200, 0, 255 };
    zcsr_gl_texture tex = zcsr_gl_texture_create(renderer, green, 1, 1);
    if (!tex) {
        printf("FAIL: fixture texture create failed\n");
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(surface);
        return 1;
    }

    zcsr_gl_clear(renderer, 1.0f, 1.0f, 1.0f, 1.0f);
    zcsr_gl_begin(renderer);

    /* Optional second argument selects the rotation preset for snapshot
     * comparison only: "45" uses the exact 16-bit-circle step rot16=8192 (45°),
     * so both tiers feed the shader the same representable angle. ctest runs
     * with no arguments and keeps the 30° fixture. */
    const int exact45 = (argc > 2 && argv[2] != NULL &&
                         argv[2][0] == '4' && argv[2][1] == '5');

#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
    /* 36 B compact record: pack rotation (16-bit circle step) and alpha
     * (8-bit) straight into the record; the entry does no 168 -> 36 convert. */
    zcsr_instance_compact rec;
    memset(&rec, 0, sizeof rec);
    rec.cx = 960.0f;
    rec.cy = 540.0f;
    rec.w  = 200.0f;
    rec.h  = 200.0f;
    rec.u0 = 0.0f; rec.v0 = 0.0f; rec.u1 = 1.0f; rec.v1 = 1.0f;
    {
        uint32_t rot16;
        uint32_t alpha8 = (uint32_t)(0.5f * ZCSR_COMPACT_ALPHA_STEPS + 0.5f);
        if (exact45) {
            /* 45°: rot16=8192 is exact on the 16-bit circle (8192/65536 turn). */
            rot16 = 8192u;
        } else {
            const float rot = 30.0f * 3.14159265358979f / 180.0f;
            float turns = rot * (1.0f / 6.28318530718f);
            float frac = turns - (float)(int)turns;
            frac += (frac < 0.0f);                       /* truncation -> [0,1) */
            rot16 = (uint32_t)(frac * ZCSR_COMPACT_ROT_STEPS + 0.5f)
                    & ZCSR_COMPACT_PACK_ROT_MASK;
        }
        rec.pack = rot16 | (alpha8 << ZCSR_COMPACT_PACK_ALPHA_SHIFT);
    }
    size_t submitted = zcsr_gl_draw_instances_compact(renderer, tex, &rec, 1);
#else
    zcsr_gl_instance inst;
    memset(&inst, 0, sizeof inst);
    inst.cx = 960.0f;
    inst.cy = 540.0f;
    inst.w  = 200.0f;
    inst.h  = 200.0f;
    inst.u0 = 0.0f; inst.v0 = 0.0f; inst.u1 = 1.0f; inst.v1 = 1.0f;
    inst.r = 1.0f; inst.g = 1.0f; inst.b = 1.0f; inst.a = 0.5f;
    inst.rotation = exact45
        ? 8192.0f / 65536.0f * 6.28318530718f
        : 30.0f * 3.14159265358979f / 180.0f;
    inst.amount = 0.0f;
    inst.mode = (float)ZCSR_MOD_MULTIPLY;
    inst.tex = tex;
    inst.z = 0.0f;
    inst.has_alpha = true;

    size_t submitted = zcsr_gl_draw_instances(renderer, tex, &inst, 1);
#endif
    if (submitted != 1) {
        printf("FAIL: rotated semi-transparent instance rejected (submitted=%zu)\n",
               submitted);
        zcsr_gl_texture_destroy(renderer, tex);
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(surface);
        return 1;
    }
    zcsr_gl_flush(renderer);

    int x = 0, y = 0;
    if (!zcsr_gl_read_rgba(renderer, px, sizeof px, &x, &y)) {
        printf("FAIL: readback failed\n");
        zcsr_gl_texture_destroy(renderer, tex);
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(surface);
        return 1;
    }

    /* glReadPixels returns rows bottom-up; submission coordinates are y-down,
     * so a world point (x, y) lands at row (height - 1 - y). */
#define CP_OFF(px_x, px_y) (((y - 1 - (px_y)) * x + (px_x)) * 4)

    /* Center pixel: 50% green over white -> green channel dominant. */
    int off = CP_OFF(960, 540);
    unsigned cr = px[off + 0], cg = px[off + 1], cb = px[off + 2];
    if (!(cg > cr + 15 && cg > cb + 15)) {
        printf("FAIL: center pixel not green-dominant: (%u,%u,%u)\n",
               cr, cg, cb);
        zcsr_gl_texture_destroy(renderer, tex);
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(surface);
        return 1;
    }

    /* Rotation evidence. The 200 px AABB spans y 440..640; point (1000,660)
     * is outside it but inside the 30 deg rotated square (inverse-rotated local
     * coords ~ (94.6, 83.9), within the +/-100 half-extents). No rotation ->
     * this pixel stays white. The probe point is tuned to the 30° fixture and
     * falls outside the 45° square, so it is skipped in the exact45 preset,
     * which exists only for cross-tier pixel comparison. */
    int rotated = exact45 ? 1 : 0;
    if (!exact45) {
        int offr = CP_OFF(1000, 660);
        unsigned rr = px[offr + 0], rg = px[offr + 1], rb = px[offr + 2];
        rotated = (rr < 250 || rg < 250 || rb < 250);
        if (!rotated) {
            printf("FAIL: rotation did not expand the quad (corner still white)\n");
            zcsr_gl_texture_destroy(renderer, tex);
            zcsr_gl_destroy(renderer);
            zcsr_surface_destroy(surface);
            return 1;
        }
    }

    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') {
        FILE* f = fopen(argv[1], "wb");
        if (!f) {
            printf("FAIL: cannot open snapshot %s\n", argv[1]);
            zcsr_gl_texture_destroy(renderer, tex);
            zcsr_gl_destroy(renderer);
            zcsr_surface_destroy(surface);
            return 1;
        }
        fwrite("ZCSRPIX1", 1, 8, f);
        fwrite(&x, sizeof x, 1, f);
        fwrite(&y, sizeof y, 1, f);
        fwrite(px, 1, (size_t)x * (size_t)y * 4, f);
        fclose(f);
    }

    printf("PASS: compact pixel rotated alpha sprite "
           "(layout=%s layout=%d color=%d sort=%d) center=(%u,%u,%u) rotated=%d\n",
           CP_LAYOUT_NAME, ZCSR_INSTANCE_LAYOUT, ZCSR_COLOR_TIER,
           ZCSR_SORT_TIER, cr, cg, cb, rotated);

    zcsr_gl_texture_destroy(renderer, tex);
    zcsr_gl_destroy(renderer);
    zcsr_surface_destroy(surface);
    return 0;
}
