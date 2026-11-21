/* modules/collision/collision.c — 2D collision primitives.
 * Pure functions, no state, no heap, no file I/O. */
#include "zcsr/collision.h"

#include <math.h>

static bool zcsr_collision_finite(float value) {
    return isfinite(value) != 0;
}

static bool zcsr_collision_box_valid(zcsr_aabb box) {
    return zcsr_collision_finite(box.x) && zcsr_collision_finite(box.y) &&
           zcsr_collision_finite(box.w) && zcsr_collision_finite(box.h) &&
           box.w > 0.0f && box.h > 0.0f;
}

static bool zcsr_collision_ray_valid(zcsr_ray ray) {
    return zcsr_collision_finite(ray.origin_x) &&
           zcsr_collision_finite(ray.origin_y) &&
           zcsr_collision_finite(ray.direction_x) &&
           zcsr_collision_finite(ray.direction_y) &&
           zcsr_collision_finite(ray.maximum_distance) &&
           ray.maximum_distance > 0.0f;
}

bool zcsr_aabb_intersect(zcsr_aabb a, zcsr_aabb b) {
    if (!zcsr_collision_box_valid(a) || !zcsr_collision_box_valid(b)) return false;
    return a.x < (b.x + b.w) &&
           (a.x + a.w) > b.x &&
           a.y < (b.y + b.h) &&
           (a.y + a.h) > b.y;
}

bool zcsr_aabb_contains_point(zcsr_aabb a, float px, float py) {
    if (!zcsr_collision_box_valid(a) ||
        !zcsr_collision_finite(px) || !zcsr_collision_finite(py)) return false;
    return px >= a.x && px < (a.x + a.w) &&
           py >= a.y && py < (a.y + a.h);
}

bool zcsr_aabb_ray_intersect(zcsr_aabb box, zcsr_ray ray, float* out_distance) {
    float t_min = 0.0f;
    float t_max = ray.maximum_distance;
    float d;

    if (!zcsr_collision_box_valid(box) || !zcsr_collision_ray_valid(ray)) return false;
    if (fabsf(ray.direction_x) < 1e-6f &&
        fabsf(ray.direction_y) < 1e-6f) return false;

    d = ray.direction_x;
    if (fabsf(d) < 1e-6f) {
        if (ray.origin_x < box.x || ray.origin_x > box.x + box.w) return false;
    } else {
        float t1 = (box.x - ray.origin_x) / d;
        float t2 = (box.x + box.w - ray.origin_x) / d;
        if (t1 > t2) {
            float swap = t1;
            t1 = t2;
            t2 = swap;
        }
        if (t1 > t_min) t_min = t1;
        if (t2 < t_max) t_max = t2;
        if (t_min > t_max) return false;
    }

    d = ray.direction_y;
    if (fabsf(d) < 1e-6f) {
        if (ray.origin_y < box.y || ray.origin_y > box.y + box.h) return false;
    } else {
        float t1 = (box.y - ray.origin_y) / d;
        float t2 = (box.y + box.h - ray.origin_y) / d;
        if (t1 > t2) {
            float swap = t1;
            t1 = t2;
            t2 = swap;
        }
        if (t1 > t_min) t_min = t1;
        if (t2 < t_max) t_max = t2;
        if (t_min > t_max) return false;
    }

    if (out_distance != 0) *out_distance = t_min;
    return true;
}

bool zcsr_circle_aabb_intersect(zcsr_circle circle, zcsr_aabb box) {
    float closest_x;
    float closest_y;
    float dx;
    float dy;
    float radius_squared;

    if (!zcsr_collision_box_valid(box) ||
        !zcsr_collision_finite(circle.center_x) ||
        !zcsr_collision_finite(circle.center_y) ||
        !zcsr_collision_finite(circle.radius) ||
        circle.radius <= 0.0f) return false;

    closest_x = circle.center_x;
    closest_y = circle.center_y;
    if (closest_x < box.x) closest_x = box.x;
    if (closest_x > box.x + box.w) closest_x = box.x + box.w;
    if (closest_y < box.y) closest_y = box.y;
    if (closest_y > box.y + box.h) closest_y = box.y + box.h;

    dx = circle.center_x - closest_x;
    dy = circle.center_y - closest_y;
    radius_squared = circle.radius * circle.radius;
    return (dx * dx + dy * dy) <= radius_squared;
}

bool zcsr_aabb_sweep(zcsr_aabb moving_box, float velocity_x, float velocity_y,
                     zcsr_aabb fixed_box, float* out_time) {
    zcsr_aabb expanded_fixed;
    zcsr_ray path;
    float hit_time = 0.0f;

    if (!zcsr_collision_box_valid(moving_box) ||
        !zcsr_collision_box_valid(fixed_box) ||
        !zcsr_collision_finite(velocity_x) ||
        !zcsr_collision_finite(velocity_y)) return false;

    if (zcsr_aabb_intersect(moving_box, fixed_box)) {
        if (out_time != 0) *out_time = 0.0f;
        return true;
    }

    if (fabsf(velocity_x) < 1e-6f && fabsf(velocity_y) < 1e-6f) return false;

    expanded_fixed.x = fixed_box.x - moving_box.w * 0.5f;
    expanded_fixed.y = fixed_box.y - moving_box.h * 0.5f;
    expanded_fixed.w = fixed_box.w + moving_box.w;
    expanded_fixed.h = fixed_box.h + moving_box.h;

    path.origin_x = moving_box.x + moving_box.w * 0.5f;
    path.origin_y = moving_box.y + moving_box.h * 0.5f;
    path.direction_x = velocity_x;
    path.direction_y = velocity_y;
    path.maximum_distance = 1.0f;

    if (!zcsr_aabb_ray_intersect(expanded_fixed, path, &hit_time)) return false;
    if (out_time != 0) *out_time = hit_time;
    return true;
}
