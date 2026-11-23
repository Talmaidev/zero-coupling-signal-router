#include "zcsr/light2d.h"

#include <math.h>
#include <stdio.h>

static int failures = 0;

static void check(int condition, const char* what) {
    if (!condition) {
        printf("FAIL: %s\n", what);
        ++failures;
    }
}

static void test_defaults(void) {
    zcsr_light2d_environment env;
    zcsr_light2d_environment_default(&env);
    check(env.ambient_r > 0.029f && env.ambient_r < 0.031f, "ambient default");
    check(env.exposure == 1.0f, "exposure default");
    check(env.ambient_occlusion == 0.0f, "AO default");
    check(!env.sun.enabled, "sun disabled by default");
    check(env.sun.cast_shadow, "sun shadow default");
    check(env.sun.shadow_length_scale == 1.0f, "shadow scale default");
    check(zcsr_light2d_environment_is_valid(&env), "default is valid");
}

static void test_validation(void) {
    zcsr_light2d_environment env;
    zcsr_light2d_environment_default(&env);

    env.exposure = 0.0f;
    check(!zcsr_light2d_environment_is_valid(&env), "zero exposure rejected");
    zcsr_light2d_environment_default(&env);

    env.ambient_occlusion = 1.5f;
    check(!zcsr_light2d_environment_is_valid(&env), "AO > 1 rejected");
    zcsr_light2d_environment_default(&env);

    env.ambient_g = -0.1f;
    check(!zcsr_light2d_environment_is_valid(&env), "negative ambient rejected");
    zcsr_light2d_environment_default(&env);

    env.sun.enabled = true;
    env.sun.dir_x = 0.0f;
    env.sun.dir_y = 0.0f;
    env.sun.dir_z = 0.0f;
    check(!zcsr_light2d_environment_is_valid(&env), "zero sun direction rejected");
    zcsr_light2d_environment_default(&env);

    env.sun.enabled = true;
    env.sun.intensity = -1.0f;
    check(!zcsr_light2d_environment_is_valid(&env), "negative sun intensity rejected");
    zcsr_light2d_environment_default(&env);

    env.sun.shadow_length_scale = -1.0f;
    check(!zcsr_light2d_environment_is_valid(&env), "negative shadow scale rejected");
}

static void test_sun_conversion(void) {
    zcsr_light2d_sun sun;
    zcsr_light light;
    zcsr_light2d_environment env;

    zcsr_light2d_environment_default(&env);
    sun = env.sun;
    check(!zcsr_light2d_sun_to_light(&sun, &light), "disabled sun converts nothing");

    sun.enabled = true;
    sun.dir_x = 0.0f;
    sun.dir_y = 0.5f;
    sun.dir_z = -1.0f;
    sun.r = 1.0f;
    sun.g = 0.9f;
    sun.b = 0.8f;
    sun.intensity = 2.0f;
    check(zcsr_light2d_sun_to_light(&sun, &light), "enabled sun converts");
    check(light.type == ZCSR_LIGHT_PARALLEL, "sun is parallel");
    check(light.x == 0.0f && light.y == 0.0f && light.height == 0.0f,
          "sun has no position");
    check(light.radius == 0.0f && light.decay == 0.0f, "sun has no falloff");
    check(light.a == 2.0f, "sun intensity carried in alpha");
    check(light.r == 1.0f && light.g == 0.9f && light.b == 0.8f,
          "sun RGB carried verbatim");
    check(fabsf(light.dir_z) > 0.85f && light.dir_z < 0.0f,
          "sun direction normalized downward");

    sun.dir_x = 0.0f;
    sun.dir_y = 0.0f;
    sun.dir_z = 0.0f;
    check(!zcsr_light2d_sun_to_light(&sun, &light), "zero sun direction rejected");
}

static void test_parallel_shadow_length(void) {
    const float pi = 3.14159265358979323846f;
    float elev15 = 15.0f * pi / 180.0f;
    float elev45 = 45.0f * pi / 180.0f;
    float elev90 = 90.0f * pi / 180.0f;
    float len15 = zcsr_light2d_parallel_shadow_length_px(
        1.0f, cosf(elev15), 0.0f, -sinf(elev15), 1.0f, 100.0f);
    float len45 = zcsr_light2d_parallel_shadow_length_px(
        1.0f, cosf(elev45), 0.0f, -sinf(elev45), 1.0f, 100.0f);
    float len90 = zcsr_light2d_parallel_shadow_length_px(
        1.0f, cosf(elev90), 0.0f, -sinf(elev90), 1.0f, 100.0f);
    float unnormalized = zcsr_light2d_parallel_shadow_length_px(
        1.0f, 2.0f * cosf(elev45), 0.0f, -2.0f * sinf(elev45), 1.0f, 100.0f);

    check(fabsf(len45 - 100.0f) < 1e-4f, "45deg shadow is height*ppu");
    check(fabsf(len15 / len45 - 3.73205f) < 1e-3f,
          "15deg shadow is 1/tan(15) times 45deg");
    check(fabsf(len90) < 1e-5f, "90deg sun casts no ground band");
    check(fabsf(unnormalized - len45) < 1e-4f,
          "unnormalized direction yields the same length");
    check(zcsr_light2d_parallel_shadow_length_px(
              0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 100.0f) == 0.0f,
          "zero-height occluder casts no band");
    check(zcsr_light2d_parallel_shadow_length_px(
              1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 100.0f) == 0.0f,
          "zero shadow scale casts no band");
    check(zcsr_light2d_parallel_shadow_length_px(
              1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 100.0f) == 100000.0f,
          "horizontal light is unbounded");
}

int main(void) {
    test_defaults();
    test_validation();
    test_sun_conversion();
    test_parallel_shadow_length();
    if (failures != 0) {
        printf("%d environment contract failure(s)\n", failures);
        return 1;
    }
    printf("PASS: light2d_environment contract\n");
    return 0;
}
