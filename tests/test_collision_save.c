/* Agent 5 — gamelogic tests: AABB collision + binary save/load (round-trip + reject paths). */
#include "zcsr/collision.h"
#include "zcsr/save.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int fails = 0;
#define CK(label, cond) do { if (!(cond)) { printf("FAIL: %s\n", label); fails++; } } while (0)

static void test_collision(void) {
    zcsr_aabb a = { 0, 0, 10, 10 };
    CK("overlap",                  zcsr_aabb_intersect(a, (zcsr_aabb){ 5, 5, 10, 10 }) == true);
    CK("separate",                 zcsr_aabb_intersect(a, (zcsr_aabb){ 20, 20, 5, 5 }) == false);
    CK("touching edge = no overlap", zcsr_aabb_intersect(a, (zcsr_aabb){ 10, 0, 5, 5 }) == false);
    CK("fully contained",          zcsr_aabb_intersect(a, (zcsr_aabb){ 2, 2, 3, 3 })  == true);
    CK("zero-size = false",        zcsr_aabb_intersect(a, (zcsr_aabb){ 5, 5, 0, 0 })  == false);
    CK("neg-size = false",         zcsr_aabb_intersect((zcsr_aabb){ 0, 0, -1, 5 }, a) == false);

    CK("point inside",             zcsr_aabb_contains_point(a, 5, 5) == true);
    CK("point outside",            zcsr_aabb_contains_point(a, 11, 5) == false);
    CK("point top-left inclusive", zcsr_aabb_contains_point(a, 0, 0) == true);
    CK("point bottom-right exclusive", zcsr_aabb_contains_point(a, 10, 10) == false);

    {
        zcsr_ray ray = { -5, 5, 1, 0, 20 };
        float distance = -1.0f;
        CK("ray hits box", zcsr_aabb_ray_intersect(a, ray, &distance) == true);
        CK("ray distance", distance == 5.0f);
        ray.origin_x = 20;
        CK("ray behind box", zcsr_aabb_ray_intersect(a, ray, &distance) == false);
        ray.origin_x = -5;
        ray.maximum_distance = 3;
        CK("ray too short", zcsr_aabb_ray_intersect(a, ray, &distance) == false);
        ray.origin_x = 5;
        ray.origin_y = 5;
        ray.maximum_distance = 20;
        CK("ray starts inside", zcsr_aabb_ray_intersect(a, ray, &distance) == true);
        CK("ray starts inside distance", distance == 0.0f);
        ray.origin_x = -5;
        ray.origin_y = 20;
        CK("ray misses box", zcsr_aabb_ray_intersect(a, ray, &distance) == false);
        ray.origin_y = 5;
        ray.direction_x = 0;
        CK("zero direction rejected", zcsr_aabb_ray_intersect(a, ray, &distance) == false);
    }

    {
        zcsr_circle circle = { 5, 5, 1 };
        CK("circle inside box", zcsr_circle_aabb_intersect(circle, a) == true);
        circle.center_x = 12;
        CK("circle outside box", zcsr_circle_aabb_intersect(circle, a) == false);
        circle.center_x = 10.5f;
        circle.radius = 0.5f;
        CK("circle touches edge", zcsr_circle_aabb_intersect(circle, a) == true);
        circle.radius = 0;
        CK("zero radius rejected", zcsr_circle_aabb_intersect(circle, a) == false);
    }

    {
        zcsr_aabb moving = { 0, 0, 2, 2 };
        zcsr_aabb fixed = { 5, 0, 2, 2 };
        float time = -1.0f;
        CK("sweep hits", zcsr_aabb_sweep(moving, 10, 0, fixed, &time) == true);
        CK("sweep time", time == 0.3f);
        moving.x = 4;
        CK("sweep starts overlapping", zcsr_aabb_sweep(moving, 10, 0, fixed, &time) == true);
        CK("sweep start time", time == 0.0f);
        moving.x = 0;
        CK("sweep zero velocity", zcsr_aabb_sweep(moving, 0, 0, fixed, &time) == false);
        CK("sweep misses", zcsr_aabb_sweep(moving, 0, 10, fixed, &time) == false);
    }
}

static void test_save(void) {
    const char payload[] = "game-state: gold=276 pop=30/200 age=castle";
    const size_t ps = sizeof payload; /* includes trailing NUL */
    uint8_t buf[128];
    uint8_t loaded[128];

    size_t written = zcsr_save_write(payload, ps, buf, sizeof buf, 7);
    CK("write returns header+size", written == 20 + ps);

    size_t read = zcsr_save_read(buf, written, loaded, sizeof loaded, 7);
    CK("read returns payload size",   read == ps);
    CK("round-trip payload matches",  read == ps && memcmp(loaded, payload, ps) == 0);

    /* corrupt one payload byte -> checksum reject */
    uint8_t save25 = buf[25]; buf[25] ^= 0xFFu;
    CK("corrupt payload rejected", zcsr_save_read(buf, written, loaded, sizeof loaded, 7) == 0);
    buf[25] = save25;

    /* bad magic */
    uint8_t save0 = buf[0]; buf[0] ^= 0xFFu;
    CK("bad magic rejected", zcsr_save_read(buf, written, loaded, sizeof loaded, 7) == 0);
    buf[0] = save0;

    /* version mismatch */
    CK("version mismatch rejected", zcsr_save_read(buf, written, loaded, sizeof loaded, 8) == 0);

    /* reserved field != 0 */
    uint8_t save16 = buf[16]; buf[16] = 1u;
    CK("reserved!=0 rejected", zcsr_save_read(buf, written, loaded, sizeof loaded, 7) == 0);
    buf[16] = save16;

    /* truncated input (smaller than header) */
    CK("truncated input rejected", zcsr_save_read(buf, 10, loaded, sizeof loaded, 7) == 0);

    /* out buffer too small on write (room for header but not payload) */
    uint8_t small[20];
    CK("write too-small out -> 0", zcsr_save_write(payload, ps, small, sizeof small, 7) == 0);

    /* state_cap too small on read */
    CK("read state_cap too small -> 0", zcsr_save_read(buf, written, loaded, ps - 1, 7) == 0);
}

int main(void) {
    test_collision();
    test_save();
    if (!fails) printf("test_gamelogic: PASS (AABB + save/load round-trip & reject paths)\n");
    return fails ? 1 : 0;
}
