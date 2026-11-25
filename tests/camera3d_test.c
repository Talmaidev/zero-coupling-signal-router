/* camera3d_test.c — perspective projection and ground-pick regression.
 *
 * Not a "runs without crashing" smoke: every check asserts a concrete result.
 * Ground is the horizontal y=0 plane in a Y-up world; a low-pitch upward ray
 * must miss instead of returning a huge coordinate from the old z=0 intersect.
 */
#include "zcsr/camera.h"
#include "zcsr/camera3d.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int fails = 0;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++fails; } \
} while (0)

static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static void test_perspective_center(void) {
    zcsr_camera3d c;
    float px = -1, py = -1, depth = -1;
    /* At the origin looking down -Z: a point 10 units ahead is screen center. */
    zcsr_camera3d_init(&c, 800, 600, 0, 0, 0, 0, 0, 60);
    CHECK(zcsr_camera3d_project(&c, 0, 0, -10, &px, &py, &depth));
    CHECK(near(px, 400, 0.01f));
    CHECK(near(py, 300, 0.01f));
    CHECK(near(depth, 10, 0.01f));
}

static void test_perspective_depth_scaling(void) {
    zcsr_camera3d c;
    float px1 = 0, px2 = 0, d1 = 0, d2 = 0;
    /* The same world offset moves closer to center with depth. */
    zcsr_camera3d_init(&c, 800, 600, 0, 0, 0, 0, 0, 60);
    CHECK(zcsr_camera3d_project(&c, 1, 0, -5, &px1, NULL, &d1));
    CHECK(zcsr_camera3d_project(&c, 1, 0, -20, &px2, NULL, &d2));
    CHECK(px1 > px2);
    CHECK(px1 - 400 > 400 - 400);
    {
        float w1 = 0, w2 = 0;
        CHECK(zcsr_camera3d_billboard(&c, 0, 0, -5, 1, 1, NULL, NULL, &w1, NULL));
        CHECK(zcsr_camera3d_billboard(&c, 0, 0, -20, 1, 1, NULL, NULL, &w2, NULL));
        CHECK(w1 > w2 * 3.0f);
    }
}

static void test_perspective_behind(void) {
    zcsr_camera3d c;
    zcsr_camera3d_init(&c, 800, 600, 0, 0, 0, 0, 0, 60);
    CHECK(!zcsr_camera3d_project(&c, 0, 0, 10, NULL, NULL, NULL));
}

static void test_pitch_down(void) {
    zcsr_camera3d c;
    float px = 0, py = 0;
    float ay, az;
    float rad = 45.0f * 3.14159265358979323846f / 180.0f;
    zcsr_camera3d_init(&c, 800, 600, 0, 0, 0, 0, 45, 60);
    /* Positive pitch looks down: the view axis is (0,-sin, -cos). */
    ay = -sinf(rad) * 10.0f;
    az = -cosf(rad) * 10.0f;
    CHECK(zcsr_camera3d_project(&c, 0.0f, ay, az, &px, &py, NULL));
    CHECK(near(px, 400, 0.5f));
    CHECK(near(py, 300, 0.5f));

    c.pitch_deg = 70.0f;
    rad = 70.0f * 3.14159265358979323846f / 180.0f;
    ay = -sinf(rad) * 10.0f;
    az = -cosf(rad) * 10.0f;
    CHECK(zcsr_camera3d_project(&c, 0.0f, ay, az, &px, &py, NULL));
    CHECK(near(px, 400, 0.5f));
    CHECK(near(py, 300, 0.5f));
}

static void test_ortho_pitch_mapping(void) {
    zcsr_camera c;
    float k;
    zcsr_camera_init(&c, 800, 600, 0, 0, 32.0f, 0.5f);
    zcsr_camera_set_pitch_deg(&c, 90.0f);
    CHECK(near(c.height_shear, 0.0f, 1e-4f));
    zcsr_camera_set_pitch_deg(&c, 45.0f);
    CHECK(near(c.height_shear, 1.0f, 1e-3f));
    zcsr_camera_set_pitch_deg(&c, 63.43f);
    k = c.height_shear;
    CHECK(near(k, 0.5f, 0.01f));
    CHECK(near(zcsr_camera_get_pitch_deg(&c), 63.43f, 0.5f));
}

static void test_screen_ground_roundtrip(void) {
    zcsr_camera3d c;
    float px, py, depth, gx, gz;
    /* 2.5D ground pick: project a y=0 point, then invert the same camera. */
    zcsr_camera3d_init(&c, 800, 600, 0, 20, 0, 0, 60, 60);
    CHECK(zcsr_camera3d_project(&c, 5, 0, 3, &px, &py, &depth));
    CHECK(zcsr_camera3d_screen_to_ground(&c, px, py, &gx, &gz));
    CHECK(near(gx, 5, 0.05f));
    CHECK(near(gz, 3, 0.05f));

    zcsr_camera3d_init(&c, 800, 600, 0, 20, 0, 35, 55, 55);
    CHECK(zcsr_camera3d_project(&c, -4, 0, -2, &px, &py, &depth));
    CHECK(zcsr_camera3d_screen_to_ground(&c, px, py, &gx, &gz));
    CHECK(near(gx, -4, 0.05f));
    CHECK(near(gz, -2, 0.05f));
}

static void test_screen_ground_up_miss(void) {
    zcsr_camera3d c;
    float gx = 0, gz = 0;
    /* Low-pitch upward ray from above the ground must miss, not return a
     * huge coordinate from a z=0 intersection. */
    zcsr_camera3d_init(&c, 800, 600, 0, 10, 0, 0, -5, 60);
    CHECK(!zcsr_camera3d_screen_to_ground(&c, 400, 300, &gx, &gz));

    /* A horizontal ray is parallel to y=0 and also misses. */
    zcsr_camera3d_init(&c, 800, 600, 0, 10, 0, 0, 0, 60);
    CHECK(!zcsr_camera3d_screen_to_ground(&c, 400, 300, &gx, &gz));
}

static void test_screen_ground_low_pitch_hit(void) {
    zcsr_camera3d c;
    float px = 0, py = 0, depth = 0, gx = 0, gz = 0;
    /* A 1-degree downward ray is valid but lands far away; it must round-trip
     * instead of being rejected by a near/far shortcut. */
    zcsr_camera3d_init(&c, 800, 600, 0, 10, 0, 0, 1, 60);
    CHECK(zcsr_camera3d_screen_to_ground(&c, 400, 300, &gx, &gz));
    CHECK(isfinite(gx) && isfinite(gz));
    CHECK(gz < -500.0f);
    CHECK(zcsr_camera3d_project(&c, gx, 0, gz, &px, &py, &depth));
    CHECK(near(px, 400, 0.5f));
    CHECK(near(py, 300, 0.5f));
}

int main(void) {
    test_perspective_center();
    test_perspective_depth_scaling();
    test_perspective_behind();
    test_pitch_down();
    test_screen_ground_roundtrip();
    test_screen_ground_up_miss();
    test_screen_ground_low_pitch_hit();
    test_ortho_pitch_mapping();
    if (fails) {
        printf("camera3d: %d check(s) failed\n", fails);
        return 1;
    }
    printf("camera3d: all checks passed\n");
    return 0;
}
