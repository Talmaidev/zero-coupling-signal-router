/* tests/test_light2d_math.c — deterministic Light2D v1 math-contract checks.
 * Pure CPU, no GL, no window: exercises the camera projection contract and
 * the canonical segment-vs-prism shadow test (the same math the light
 * shader runs, mirrored host-side in integration/testmain.c).
 * Covers the math-model verification matrix items that need no GPU:
 *   projection/ground-inverse round-trip, elevated-point round-trip via the
 *   documented known-z inverse, k-shift screen law, view rect, zoom clamp,
 *   sort key ordering, slab-test occlusion cases incl. boundary/parallel. */
#include "zcsr/camera.h"

#include <math.h>
#include <stdio.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        ++checks;                                                      \
        if (!(cond)) {                                                 \
            ++failures;                                                \
            printf("FAIL: %s (line %d)\n", name, __LINE__);            \
        }                                                              \
    } while (0)

static int approx(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

/* ---- canonical segment-vs-prism slab test (Geometry Proof v1 semantics:
 * occluded iff segment L->P, t in (eps, 1-eps), crosses footprint x [0,H]).
 * Kept in sync with the GLSL zcsr_ray_blocked and testmain seg_blocked. */
static int seg_blocked(float lx, float ly, float lz,
                       float px, float py, float pz,
                       float minx, float miny, float maxx, float maxy, float h)
{
    const float dx = px - lx;
    const float dy = py - ly;
    const float dz = pz - lz;
    float t0 = 1e-4f;
    float t1 = 1.0f - 1e-4f;
    float ta, tb;
    if (fabsf(dx) < 1e-6f) {
        if (lx < minx || lx > maxx) return 0;
    } else {
        ta = (minx - lx) / dx;
        tb = (maxx - lx) / dx;
        if (ta > tb) { float s = ta; ta = tb; tb = s; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
    }
    if (fabsf(dy) < 1e-6f) {
        if (ly < miny || ly > maxy) return 0;
    } else {
        ta = (miny - ly) / dy;
        tb = (maxy - ly) / dy;
        if (ta > tb) { float s = ta; ta = tb; tb = s; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
    }
    if (fabsf(dz) < 1e-6f) {
        if (lz < 0.0f || lz > h) return 0;
    } else {
        ta = (0.0f - lz) / dz;
        tb = (h - lz) / dz;
        if (ta > tb) { float s = ta; ta = tb; tb = s; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
    }
    return (t0 < t1) ? 1 : 0;
}

int main(void)
{
    const float k = 0.5f;
    const float ppu = 96.0f;
    zcsr_camera cam;

    /* --- camera contract --- */
    zcsr_camera_init(&cam, 1280, 720, 4.0f, 0.0f, ppu, k);
    CHECK(approx(cam.center_x, 4.0f) && approx(cam.center_y, 0.0f), "init center");
    CHECK(approx(cam.px_per_unit, 96.0f) && cam.view_w == 1280 && cam.view_h == 720,
          "init view/ppu");
    CHECK(approx(cam.height_shear, 0.5f), "init shear");

    zcsr_camera_set_zoom(&cam, 4.0f);
    CHECK(approx(cam.px_per_unit, 8.0f), "zoom clamps low to 8");
    zcsr_camera_set_zoom(&cam, 5000.0f);
    CHECK(approx(cam.px_per_unit, 1024.0f), "zoom clamps high to 1024");
    zcsr_camera_set_zoom(&cam, ppu);

    {
        float minx, miny, maxx, maxy;
        zcsr_camera_move_to(&cam, 2.0f, 1.0f);
        zcsr_camera_view_rect(&cam, &minx, &miny, &maxx, &maxy);
        /* half extents = (view/2)/ppu = 640/96, 360/96 */
        CHECK(approx(minx, 2.0f - 640.0f / 96.0f) && approx(maxx, 2.0f + 640.0f / 96.0f),
              "view_rect x");
        CHECK(approx(miny, 1.0f - 360.0f / 96.0f) && approx(maxy, 1.0f + 360.0f / 96.0f),
              "view_rect y");
        zcsr_camera_move_to(&cam, 4.0f, 0.0f);
    }

    /* --- projection ground round-trip (z=0): project then ground-inverse --- */
    {
        const float pts[4][2] = { { 0.0f, 0.0f }, { -1.35f, 0.65f },
                                  { 2.5f, -1.0f }, { 9.0f, 2.0f } };
        int i;
        for (i = 0; i < 4; ++i) {
            float px, py, wx, wy;
            zcsr_camera_world_to_px(&cam, pts[i][0], pts[i][1], 0.0f, &px, &py);
            zcsr_camera_px_to_world(&cam, px, py, &wx, &wy);
            CHECK(approx(wx, pts[i][0]) && approx(wy, pts[i][1]),
                  "ground round-trip");
        }
    }

    /* --- elevated round-trip via the documented known-z inverse:
     * world_to_px(wz) then x = cx + (sx-W/2)/ppu, y = cy + (sy-H/2)/ppu + k*z --- */
    {
        const float wx = 2.5f, wy = -0.25f, wz = 2.0f;
        float px, py, rx, ry;
        zcsr_camera_world_to_px(&cam, wx, wy, wz, &px, &py);
        rx = cam.center_x + (px - (float)cam.view_w * 0.5f) / cam.px_per_unit;
        ry = cam.center_y + (py - (float)cam.view_h * 0.5f) / cam.px_per_unit
             + cam.height_shear * wz;
        CHECK(approx(rx, wx) && approx(ry, wy), "elevated round-trip");
        /* screen law: rise = k*z*ppu above the ground projection */
        {
            float gx, gy;
            zcsr_camera_world_to_px(&cam, wx, wy, 0.0f, &gx, &gy);
            CHECK(approx(py, gy - cam.height_shear * wz * cam.px_per_unit),
                  "shear screen law");
        }
    }

    /* --- k-shift law: changing k moves screen y by dk*z*ppu, x untouched ---
     * (the G-buffer half of this invariant — world data unaffected — is a
     * scene-level property, runtime-verified by the Q3 double-k probes) */
    {
        float ax, ay, bx, by;
        zcsr_camera_world_to_px(&cam, 1.0f, 1.0f, 2.0f, &ax, &ay);
        cam.height_shear += 0.25f;
        zcsr_camera_world_to_px(&cam, 1.0f, 1.0f, 2.0f, &bx, &by);
        /* larger k lifts the point: screen y DECREASES by dk*z*ppu */
        CHECK(approx(ay - by, 0.25f * 2.0f * ppu) && approx(ax, bx),
              "k-shift law");
        cam.height_shear = k;
    }

    /* --- shear sort key q = k*(y-cy) + z: far-to-near ordering example --- */
    {
        const float cy = 0.0f;
        const float q_floor = k * (1.0f - cy) + 0.0f;   /* floor at y=1 */
        const float q_boxside = k * (0.5f - cy) + 1.0f; /* box face y=0.5 z=1 */
        const float q_top = k * (0.5f - cy) + 2.0f;     /* box top z=2 */
        CHECK(q_floor < q_boxside && q_boxside < q_top, "sort key ascending");
    }

    /* --- slab test: the four sealed proof cases --- */
    CHECK(seg_blocked(0, 0, 3, 7, 0, 0, 2, -0.5f, 3, 0.5f, 2) == 1,
          "proof L=(0,0,3) P=(7,0,0) occluded");
    CHECK(seg_blocked(0, 0, 3, 10, 0, 0, 2, -0.5f, 3, 0.5f, 2) == 0,
          "proof L=(0,0,3) P=(10,0,0) clear");
    CHECK(seg_blocked(0, 0, 5, 4, 0, 0, 2, -0.5f, 3, 0.5f, 2) == 1,
          "proof L=(0,0,5) P=(4,0,0) occluded");
    CHECK(seg_blocked(0, 0, 5, 7, 0, 0, 2, -0.5f, 3, 0.5f, 2) == 0,
          "proof L=(0,0,5) P=(7,0,0) clear");

    /* --- slab test boundaries --- */
    /* parallel ray along x, origin inside the y/z slabs: pure x-slab test */
    CHECK(seg_blocked(0, 0, 1, 7, 0, 1, 2, -0.5f, 3, 0.5f, 2) == 1,
          "parallel-ray through box");
    CHECK(seg_blocked(0, 3, 1, 7, 3, 1, 2, -0.5f, 3, 0.5f, 2) == 0,
          "parallel-ray outside y slab");
    /* ray below the light along +x at ground level clears the box only past
     * the shadow tip (tip x = max_x * lz / (lz - 0) ... geometric check via
     * the sealed tip law: L_z=3 -> 9) */
    CHECK(seg_blocked(0, 0, 3, 8.5f, 0, 0, 2, -0.5f, 3, 0.5f, 2) == 1,
          "inside tip x=9");
    CHECK(seg_blocked(0, 0, 3, 9.5f, 0, 0, 2, -0.5f, 3, 0.5f, 2) == 0,
          "outside tip x=9");
    /* receiver exactly at the light position: segment degenerates -> clear */
    CHECK(seg_blocked(0, 0, 3, 0, 0, 3, 2, -0.5f, 3, 0.5f, 2) == 0,
          "degenerate segment clear");

    printf("light2d_math: %d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
