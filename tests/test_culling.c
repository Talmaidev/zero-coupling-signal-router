/* Culling unit tests — AABB overlap, linear cull, uniform grid build/query, large boxes
 * spanning cells, and linear/grid agreement. Pure logic, headless. Uses CHECK (not assert)
 * so the checks run in Release too. */
#include "zcsr/culling.h"

#include <stdio.h>
#include <string.h>

#define NBOX 64

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); return 1; } } while (0)

static int contains(const uint32_t* idx, size_t n, uint32_t target) {
    for (size_t i = 0; i < n; ++i) if (idx[i] == target) return 1;
    return 0;
}

int main(void) {
    zcsr_aabb_t boxes[NBOX];
    uint32_t lin[NBOX];
    uint32_t grp[NBOX];
    uint8_t  seen[NBOX];
    uint32_t head[16 * 16];
    uint32_t next[NBOX];
    zcsr_grid_t grid;
    zcsr_view_t view;
    size_t i, nlin, ngrp;

    /* AABB overlap: touching (inclusive), overlapping, outside, containing. */
    zcsr_aabb_t a = { 0, 0, 10, 10 };
    CHECK(zcsr_aabb_in_view(&a, &(zcsr_view_t){ 10, 10, 20, 20 }));
    CHECK(zcsr_aabb_in_view(&a, &(zcsr_view_t){ -5, -5, 5, 5 }));
    CHECK(!zcsr_aabb_in_view(&a, &(zcsr_view_t){ 11, 11, 20, 20 }));
    CHECK(zcsr_aabb_in_view(&a, &(zcsr_view_t){ -100, -100, 100, 100 }));

    /* Boxes spread over a 160x160 world, 10px cells (16x16). */
    for (i = 0; i < NBOX; ++i) {
        float x = (float)((i * 13) % 150);
        float y = (float)((i * 7) % 150);
        boxes[i] = (zcsr_aabb_t){ x, y, x + 8, y + 8 };
    }
    boxes[0] = (zcsr_aabb_t){ 5, 5, 80, 80 }; /* large box spanning many cells */

    grid = (zcsr_grid_t){ 0, 0, 10, 10, 16, 16, head, next };
    size_t inserts = zcsr_grid_build(&grid, boxes, NBOX);
    CHECK(inserts >= NBOX);

    /* View covering the top-left 60x60 region. */
    view = (zcsr_view_t){ 0, 0, 60, 60 };
    nlin = zcsr_cull_linear(boxes, NBOX, &view, lin, NBOX);
    ngrp = zcsr_grid_query(&grid, boxes, NBOX, &view, grp, NBOX, seen);
    CHECK(nlin > 0 && nlin <= NBOX);
    CHECK(ngrp == nlin);
    CHECK(contains(lin, nlin, 0));
    CHECK(contains(grp, ngrp, 0));
    for (i = 0; i < ngrp; ++i) CHECK(contains(lin, nlin, grp[i]));

    /* Tiny view only the large box reaches. */
    view = (zcsr_view_t){ 50, 50, 55, 55 };
    nlin = zcsr_cull_linear(boxes, NBOX, &view, lin, NBOX);
    ngrp = zcsr_grid_query(&grid, boxes, NBOX, &view, grp, NBOX, seen);
    CHECK(ngrp == nlin);
    CHECK(contains(lin, nlin, 0));

    /* View outside the world: nothing. */
    view = (zcsr_view_t){ 300, 300, 400, 400 };
    nlin = zcsr_cull_linear(boxes, NBOX, &view, lin, NBOX);
    ngrp = zcsr_grid_query(&grid, boxes, NBOX, &view, grp, NBOX, seen);
    CHECK(nlin == 0 && ngrp == 0);

    /* Coverage fraction: 80x80 / 160x160 = 0.25. */
    float cov = zcsr_view_coverage(&grid, &(zcsr_view_t){ 0, 0, 80, 80 });
    CHECK(cov > 0.24f && cov < 0.26f);

    printf("test_culling: PASS (linear/grid agree on all views)\n");
    return 0;
}
