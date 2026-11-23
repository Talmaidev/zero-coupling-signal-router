#include "zcsr/light2d.h"

#include <math.h>

/* Pure environment contract: no GL, no renderer calls, no heap. This file exists
 * so zcsr_light2d_environment logic can be unit-tested without a GL context. */

void zcsr_light2d_environment_default(zcsr_light2d_environment* out) {
    if (out == NULL) return;
    out->ambient_r = 0.03f;
    out->ambient_g = 0.03f;
    out->ambient_b = 0.03f;
    out->exposure = 1.0f;
    out->ambient_occlusion = 0.0f;
    out->sun.enabled = false;
    out->sun.cast_shadow = true;
    out->sun.dir_x = 0.0f;
    out->sun.dir_y = 0.5f;
    out->sun.dir_z = -1.0f;
    out->sun.r = 1.0f;
    out->sun.g = 1.0f;
    out->sun.b = 1.0f;
    out->sun.intensity = 1.0f;
    out->sun.shadow_length_scale = 1.0f;
}

static bool finite_nonnegative(float v) {
    return isfinite(v) && v >= 0.0f;
}

static bool sun_direction_valid(const zcsr_light2d_sun* sun) {
    float len2;
    if (sun == NULL) return false;
    if (!isfinite(sun->dir_x) || !isfinite(sun->dir_y) || !isfinite(sun->dir_z)) {
        return false;
    }
    len2 = sun->dir_x * sun->dir_x + sun->dir_y * sun->dir_y +
           sun->dir_z * sun->dir_z;
    return len2 > 1e-12f;
}

bool zcsr_light2d_environment_is_valid(const zcsr_light2d_environment* env) {
    if (env == NULL) return false;
    if (!finite_nonnegative(env->ambient_r) ||
        !finite_nonnegative(env->ambient_g) ||
        !finite_nonnegative(env->ambient_b)) {
        return false;
    }
    if (!isfinite(env->exposure) || env->exposure <= 0.0f) return false;
    if (!isfinite(env->ambient_occlusion) ||
        env->ambient_occlusion < 0.0f || env->ambient_occlusion > 1.0f) {
        return false;
    }
    if (!isfinite(env->sun.intensity) || env->sun.intensity < 0.0f) return false;
    if (!finite_nonnegative(env->sun.r) ||
        !finite_nonnegative(env->sun.g) ||
        !finite_nonnegative(env->sun.b)) {
        return false;
    }
    if (!isfinite(env->sun.shadow_length_scale) ||
        env->sun.shadow_length_scale < 0.0f) {
        return false;
    }
    if (env->sun.enabled && !sun_direction_valid(&env->sun)) return false;
    return true;
}

bool zcsr_light2d_sun_to_light(const zcsr_light2d_sun* sun, zcsr_light* out) {
    float len;
    if (sun == NULL || out == NULL || !sun->enabled) return false;
    if (!sun_direction_valid(sun)) return false;
    if (!finite_nonnegative(sun->r) || !finite_nonnegative(sun->g) ||
        !finite_nonnegative(sun->b) || !isfinite(sun->intensity) ||
        sun->intensity < 0.0f) {
        return false;
    }
    len = sqrtf(sun->dir_x * sun->dir_x + sun->dir_y * sun->dir_y +
                sun->dir_z * sun->dir_z);
    out->x = 0.0f;
    out->y = 0.0f;
    out->height = 0.0f;
    out->radius = 0.0f;
    out->decay = 0.0f;
    out->dir_x = sun->dir_x / len;
    out->dir_y = sun->dir_y / len;
    out->dir_z = sun->dir_z / len;
    out->inner_angle = 0.0f;
    out->outer_angle = 0.0f;
    out->type = ZCSR_LIGHT_PARALLEL;
    out->r = sun->r;
    out->g = sun->g;
    out->b = sun->b;
    out->a = sun->intensity;
    return true;
}

float zcsr_light2d_parallel_shadow_length_px(float occluder_height,
                                              float dir_x,
                                              float dir_y,
                                              float dir_z,
                                              float shadow_length_scale,
                                              float ppu) {
    float len;
    float horizontal;
    float vertical;

    if (!isfinite(occluder_height) || occluder_height <= 0.0f) return 0.0f;
    if (!isfinite(shadow_length_scale) || shadow_length_scale <= 0.0f) return 0.0f;
    if (!isfinite(ppu) || ppu <= 0.0f) return 0.0f;
    if (!isfinite(dir_x) || !isfinite(dir_y) || !isfinite(dir_z)) return 0.0f;

    len = sqrtf(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
    if (!(len > 1e-12f)) return 0.0f;
    horizontal = sqrtf(dir_x * dir_x + dir_y * dir_y) / len;
    vertical = fabsf(dir_z) / len;
    if (vertical <= 1e-6f) return 100000.0f;
    return occluder_height * (horizontal / vertical) * shadow_length_scale * ppu;
}
