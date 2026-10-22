/* zcsr culling — viewport culling for 2D AABBs. Pure logic, no GL, no heap.
 *
 * Two strategies:
 *   - linear: test every box against the view (O(n), cache-friendly).
 *   - uniform grid: hash boxes into fixed-size cells, then walk only the cells the
 *     view overlaps (fast when the view covers a small fraction of the world).
 *
 * The caller owns all memory: the box array, the grid scratch, and the output index list.
 */
#ifndef ZCSR_CULLING_H
#define ZCSR_CULLING_H

#include <stdint.h>
#include <stddef.h>

/* Axis-aligned box in world/pixel coordinates. */
typedef struct {
    float min_x, min_y;
    float max_x, max_y;
} zcsr_aabb_t;

/* Visible region (camera/viewport) in the same coordinate space. */
typedef struct {
    float min_x, min_y;
    float max_x, max_y;
} zcsr_view_t;

/* Returns 1 if the box intersects the view (inclusive edges), else 0. */
int zcsr_aabb_in_view(const zcsr_aabb_t* box, const zcsr_view_t* view);

/* Linear cull: test every box. Writes the indices of visible boxes into out_indices and
 * returns the count. Stops early if out_capacity is reached. */
size_t zcsr_cull_linear(const zcsr_aabb_t* boxes, size_t count,
                        const zcsr_view_t* view,
                        uint32_t* out_indices, size_t out_capacity);

/* ---- uniform grid ---- */
typedef struct {
    float origin_x, origin_y;   /* world coordinate of cell (0,0) corner */
    float cell_w, cell_h;       /* cell size (pixels) */
    uint32_t cols, rows;        /* grid dimensions */
    /* Packed linked-list storage. cell_head[c] is the first box index in cell c, or
     * ZCSR_CULL_EMPTY; next[b] is the next box in that cell, or ZCSR_CULL_EMPTY. */
    uint32_t* cell_head;        /* length cols*rows */
    uint32_t* next;             /* length box_count */
} zcsr_grid_t;

#define ZCSR_CULL_EMPTY 0xFFFFFFFFu

/* Build a grid: clear the heads, then insert each box into every cell it overlaps.
 * Returns the number of (box, cell) insertions performed. A box larger than a cell is
 * inserted into multiple cells. The caller must size cell_head and next. */
size_t zcsr_grid_build(zcsr_grid_t* grid, const zcsr_aabb_t* boxes, size_t count);

/* Query the grid for boxes overlapping the view. Writes de-duplicated visible box indices
 * into out_indices (the 'seen' array, length box_count, is used to dedupe and is cleared
 * internally). A box is returned only if its AABB truly intersects the view, since a large
 * box can be linked from a cell just outside the view. Returns the visible count. */
size_t zcsr_grid_query(const zcsr_grid_t* grid, const zcsr_aabb_t* boxes, size_t box_count,
                       const zcsr_view_t* view,
                       uint32_t* out_indices, size_t out_capacity,
                       uint8_t* seen);

/* Fraction of the world covered by the view (0..1), to choose a strategy. */
float zcsr_view_coverage(const zcsr_grid_t* grid, const zcsr_view_t* view);

#endif /* ZCSR_CULLING_H */
