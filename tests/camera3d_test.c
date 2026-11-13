/* camera3d_test.c — 透视相机与俯仰角的最小行为验证。
 * 不是"跑起来不崩"：每一条都断言具体的投影结果。 */
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
    /* 位于原点，看向 -Z：正前方 10 单位处必须落在视口中心 */
    zcsr_camera3d_init(&c, 800, 600, 0, 0, 0, 0, 0, 60);
    CHECK(zcsr_camera3d_project(&c, 0, 0, -10, &px, &py, &depth));
    CHECK(near(px, 400, 0.01f));
    CHECK(near(py, 300, 0.01f));
    CHECK(near(depth, 10, 0.01f));
}

static void test_perspective_depth_scaling(void) {
    zcsr_camera3d c;
    float px1 = 0, px2 = 0, d1 = 0, d2 = 0;
    /* 同样的 world 偏移，越远在屏幕上越靠中间（这才是透视，正交不会这样） */
    zcsr_camera3d_init(&c, 800, 600, 0, 0, 0, 0, 0, 60);
    CHECK(zcsr_camera3d_project(&c, 1, 0, -5, &px1, NULL, &d1));
    CHECK(zcsr_camera3d_project(&c, 1, 0, -20, &px2, NULL, &d2));
    CHECK(px1 > px2);
    CHECK(px1 - 400 > 400 - 400); /* 近处偏离中心更多 */
    /* billboard 尺寸随距离缩小 */
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
    /* 相机背后（+Z 方向）不可见 */
    CHECK(!zcsr_camera3d_project(&c, 0, 0, 10, NULL, NULL, NULL));
}

static void test_pitch_down(void) {
    zcsr_camera3d c;
    float px = 0, py = 0;
    float ay, az;
    float rad = 45.0f * 3.14159265358979323846f / 180.0f;
    zcsr_camera3d_init(&c, 800, 600, 0, 0, 0, 0, 45, 60);
    /* 俯视 45 度：沿视线轴 10 单位远的点必须落在画面中心。
     * 轴方向 = (0, -sin45, -cos45)（positive pitch = looking down）。 */
    ay = -sinf(rad) * 10.0f;
    az = -cosf(rad) * 10.0f;
    CHECK(zcsr_camera3d_project(&c, 0.0f, ay, az, &px, &py, NULL));
    CHECK(near(px, 400, 0.5f));
    CHECK(near(py, 300, 0.5f));

    /* 换一个俯仰角再验证，避免只在 45 度上碰巧成立 */
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
    /* 俯仰角只是 height_shear 的另一种写法：90 度 = 纯顶视 */
    zcsr_camera_init(&c, 800, 600, 0, 0, 32.0f, 0.5f);
    zcsr_camera_set_pitch_deg(&c, 90.0f);
    CHECK(near(c.height_shear, 0.0f, 1e-4f));
    zcsr_camera_set_pitch_deg(&c, 45.0f);
    CHECK(near(c.height_shear, 1.0f, 1e-3f));
    zcsr_camera_set_pitch_deg(&c, 63.43f); /* 旧 debug 视角 */
    k = c.height_shear;
    CHECK(near(k, 0.5f, 0.01f));
    CHECK(near(zcsr_camera_get_pitch_deg(&c), 63.43f, 0.5f));
}

int main(void) {
    test_perspective_center();
    test_perspective_depth_scaling();
    test_perspective_behind();
    test_pitch_down();
    test_ortho_pitch_mapping();
    if (fails) { printf("camera3d: %d check(s) failed\n", fails); return 1; }
    printf("camera3d: all checks passed\n");
    return 0;
}
