#include "zcsr/culling.h"

int zcsr_aabb_in_view(const zcsr_aabb_t* b, const zcsr_view_t* v) {
    if (!b || !v) return 0;
    return b->min_x <= v->max_x && b->max_x >= v->min_x &&
           b->min_y <= v->max_y && b->max_y >= v->min_y;
}

size_t zcsr_cull_linear(const zcsr_aabb_t* boxes, size_t count,
                        const zcsr_view_t* view,
                        uint32_t* out_indices, size_t out_capacity) {
    size_t i, n = 0;
    if (!boxes || !view || !out_indices) return 0;
    for (i = 0; i < count; ++i) {
        if (zcsr_aabb_in_view(&boxes[i], view)) {
            if (n >= out_capacity) return n;
            out_indices[n++] = (uint32_t)i;
        }
    }
    return n;
}

static uint32_t zcsr_clamp_i(int v, int lo, int hi) {
    if (v < lo) return (uint32_t)lo;
    if (v > hi) return (uint32_t)hi;
    return (uint32_t)v;
}

size_t zcsr_grid_build(zcsr_grid_t* g, const zcsr_aabb_t* boxes, size_t count) {
    uint32_t cells, c, r;
    size_t i, inserts = 0;
    if (!g || !boxes || !g->cell_head || !g->next) return 0;
    cells = g->cols * g->rows;
    for (c = 0; c < cells; ++c) g->cell_head[c] = ZCSR_CULL_EMPTY;

    for (i = 0; i < count; ++i) {
        const zcsr_aabb_t* b = &boxes[i];
        int c0 = (int)((b->min_x - g->origin_x) / g->cell_w);
        int c1 = (int)((b->max_x - g->origin_x) / g->cell_w);
        int r0 = (int)((b->min_y - g->origin_y) / g->cell_h);
        int r1 = (int)((b->max_y - g->origin_y) / g->cell_h);
        uint32_t x0 = zcsr_clamp_i(c0, 0, (int)g->cols - 1);
        uint32_t x1 = zcsr_clamp_i(c1, 0, (int)g->cols - 1);
        uint32_t y0 = zcsr_clamp_i(r0, 0, (int)g->rows - 1);
        uint32_t y1 = zcsr_clamp_i(r1, 0, (int)g->rows - 1);
        g->next[i] = ZCSR_CULL_EMPTY;
        for (r = y0; r <= y1; ++r) {
            for (c = x0; c <= x1; ++c) {
                uint32_t cell = r * g->cols + c;
                g->next[i] = g->cell_head[cell];
                g->cell_head[cell] = (uint32_t)i;
                ++inserts;
            }
        }
    }
    return inserts;
}

size_t zcsr_grid_query(const zcsr_grid_t* g, const zcsr_aabb_t* boxes, size_t box_count,
                       const zcsr_view_t* view,
                       uint32_t* out_indices, size_t out_capacity,
                       uint8_t* seen) {
    int c0, c1, r0, r1;
    uint32_t x0, x1, y0, y1, r, c;
    size_t i, n = 0;
    if (!g || !boxes || !view || !out_indices || !seen) return 0;
    for (i = 0; i < box_count; ++i) seen[i] = 0;

    c0 = (int)((view->min_x - g->origin_x) / g->cell_w);
    c1 = (int)((view->max_x - g->origin_x) / g->cell_w);
    r0 = (int)((view->min_y - g->origin_y) / g->cell_h);
    r1 = (int)((view->max_y - g->origin_y) / g->cell_h);
    x0 = zcsr_clamp_i(c0, 0, (int)g->cols - 1);
    x1 = zcsr_clamp_i(c1, 0, (int)g->cols - 1);
    y0 = zcsr_clamp_i(r0, 0, (int)g->rows - 1);
    y1 = zcsr_clamp_i(r1, 0, (int)g->rows - 1);

    for (r = y0; r <= y1; ++r) {
        for (c = x0; c <= x1; ++c) {
            uint32_t idx = g->cell_head[r * g->cols + c];
            while (idx != ZCSR_CULL_EMPTY) {
                if (!seen[idx] && zcsr_aabb_in_view(&boxes[idx], view)) {
                    seen[idx] = 1;
                    if (n < out_capacity) out_indices[n++] = idx;
                }
                idx = g->next[idx];
            }
        }
    }
    return n;
}

float zcsr_view_coverage(const zcsr_grid_t* g, const zcsr_view_t* v) {
    float world_w, world_h, view_w, view_h;
    if (!g || !v) return 1.0f;
    world_w = g->cell_w * (float)g->cols;
    world_h = g->cell_h * (float)g->rows;
    view_w = v->max_x - v->min_x;
    view_h = v->max_y - v->min_y;
    if (world_w <= 0 || world_h <= 0) return 1.0f;
    if (view_w < 0) view_w = 0;
    if (view_h < 0) view_h = 0;
    if (view_w > world_w) view_w = world_w;
    if (view_h > world_h) view_h = world_h;
    return (view_w * view_h) / (world_w * world_h);
}
