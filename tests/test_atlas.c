/* Atlas unit tests — shelf packing, no-overlap, region UV validity, overflow handling, blit.
 * Pure logic, runs headless. */
#include "zcsr/texture_atlas.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MAX_ITEMS 64
#define ATLAS_W   128
#define ATLAS_H   128

int main(void) {
    zcsr_atlas_item      items[MAX_ITEMS];
    zcsr_atlas_placement placements[MAX_ITEMS];
    zcsr_atlas_region    regions[MAX_ITEMS];
    uint16_t used_w = 0, used_h = 0;
    uint32_t n = 20, i, j;

    /* items with varying sizes and ids */
    for (i = 0; i < n; ++i) {
        items[i].id = i;
        items[i].w = (uint16_t)(4 + (i * 3) % 20);
        items[i].h = (uint16_t)(4 + (i * 5) % 24);
    }

    uint32_t placed = zcsr_atlas_pack(items, n, ATLAS_W, ATLAS_H,
                                      placements, &used_w, &used_h);
    assert(placed == n);
    assert(used_w <= ATLAS_W && used_h <= ATLAS_H);
    assert(used_w > 0 && used_h > 0);

    /* every placement is inside the atlas and non-negative */
    for (i = 0; i < n; ++i) {
        assert(placements[i].x >= 0 && placements[i].y >= 0);
        assert(placements[i].x + placements[i].w <= used_w);
        assert(placements[i].y + placements[i].h <= used_h);
    }

    /* no two rectangles overlap (1px gutter respected) */
    for (i = 0; i < n; ++i) {
        for (j = i + 1; j < n; ++j) {
            const zcsr_atlas_placement* a = &placements[i];
            const zcsr_atlas_placement* b = &placements[j];
            int overlap = (a->x < b->x + b->w && a->x + a->w > b->x &&
                           a->y < b->y + b->h && a->y + a->h > b->y);
            assert(!overlap);
        }
    }

    /* region table: all UVs normalized and non-degenerate */
    zcsr_atlas_build_regions(items, placements, n, used_w, used_h, regions);
    for (i = 0; i < n; ++i) {
        zcsr_atlas_region r = zcsr_atlas_lookup(regions, items[i].id);
        assert(r.u0 >= 0.0f && r.v0 >= 0.0f);
        assert(r.u1 <= 1.0001f && r.v1 <= 1.0001f);
        assert(r.u1 > r.u0 && r.v1 > r.v0);
    }

    /* an item wider than the atlas cannot be placed */
    {
        zcsr_atlas_item big[1] = { { 0, 200, 10 } };
        zcsr_atlas_placement pbig[1];
        uint32_t p = zcsr_atlas_pack(big, 1, ATLAS_W, ATLAS_H, pbig, &used_w, &used_h);
        assert(p == 0);
        assert(pbig[0].x < 0);
    }

    /* blit copies pixels into the atlas buffer */
    {
        static uint8_t atlas_px[ATLAS_W * ATLAS_H * 4];
        static uint8_t src[4 * 4 * 4];
        memset(src, 200, sizeof src);
        memset(atlas_px, 0, sizeof atlas_px);
        zcsr_atlas_blit(atlas_px, ATLAS_W, 2, 3, src, 4, 4);
        assert(atlas_px[(3 * ATLAS_W + 2) * 4] == 200);
    }

    printf("atlas: OK\n");
    return 0;
}
