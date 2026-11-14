/* modules/collision/collision.c — AABB collision.
 * Pure functions, no state, no heap, no file I/O. */
#include "zcsr/collision.h"

bool zcsr_aabb_intersect(zcsr_aabb a, zcsr_aabb b) {
    if (a.w <= 0.0f || a.h <= 0.0f || b.w <= 0.0f || b.h <= 0.0f) return false;
    return a.x < (b.x + b.w) &&
           (a.x + a.w) > b.x &&
           a.y < (b.y + b.h) &&
           (a.y + a.h) > b.y;
}

bool zcsr_aabb_contains_point(zcsr_aabb a, float px, float py) {
    if (a.w <= 0.0f || a.h <= 0.0f) return false;
    return px >= a.x && px < (a.x + a.w) &&
           py >= a.y && py < (a.y + a.h);
}
