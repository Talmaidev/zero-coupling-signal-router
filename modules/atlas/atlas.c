/* Texture atlas implementation — shelf packing (NFDH), region table, and RGBA blit.
 * Pure logic, no GL. Includes only the texture_atlas contract. No runtime heap. */
#include "zcsr/texture_atlas.h"

enum { ZCSR_GUTTER = 1 }; /* 1px gap between images to prevent bilinear bleed */

/* In-place insertion sort by height descending (stable; item counts are small enough). */
static void zcsr_sort_height_desc(zcsr_atlas_item* items, uint32_t count) {
    uint32_t i;
    for (i = 1; i < count; ++i) {
        zcsr_atlas_item key = items[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && items[j].h < key.h) {
            items[j + 1] = items[j];
            --j;
        }
        items[j + 1] = key;
    }
}

uint32_t zcsr_atlas_pack(zcsr_atlas_item* items, uint32_t count,
                         uint16_t max_w, uint16_t max_h,
                         zcsr_atlas_placement* placements,
                         uint16_t* used_w, uint16_t* used_h) {
    int32_t shelf_x = 0, shelf_top = 0, shelf_h = 0;
    int32_t content_w = 0, content_h = 0;
    uint32_t placed = 0, i;

    zcsr_sort_height_desc(items, count);

    for (i = 0; i < count; ++i) {
        int32_t w = items[i].w, h = items[i].h;
        int32_t px, py;

        if (shelf_x + w <= max_w) {
            /* fits on the current shelf */
            px = shelf_x;
            py = shelf_top;
        } else {
            /* open a new shelf below */
            int32_t new_top = shelf_top + shelf_h + ZCSR_GUTTER;
            if (w > max_w || new_top + h > max_h) {
                placements[i] = (zcsr_atlas_placement){ -1, 0, 0, 0 };
                continue;
            }
            shelf_top = new_top;
            shelf_x = 0;
            shelf_h = 0;
            px = 0;
            py = shelf_top;
        }

        placements[i] = (zcsr_atlas_placement){
            (int16_t)px, (int16_t)py, items[i].w, items[i].h
        };
        shelf_x = px + w + ZCSR_GUTTER;
        if (h > shelf_h) shelf_h = h;
        if (px + w > content_w) content_w = px + w;
        if (py + h > content_h) content_h = py + h;
        ++placed;
    }

    if (used_w) *used_w = (uint16_t)content_w;
    if (used_h) *used_h = (uint16_t)content_h;
    return placed;
}

void zcsr_atlas_build_regions(const zcsr_atlas_item* items,
                              const zcsr_atlas_placement* placements, uint32_t count,
                              uint16_t atlas_w, uint16_t atlas_h,
                              zcsr_atlas_region* regions) {
    float aw = (float)atlas_w, ah = (float)atlas_h;
    uint32_t i;
    for (i = 0; i < count; ++i) {
        float x, y, w, h;
        if (placements[i].x < 0) continue;
        x = (float)placements[i].x;
        y = (float)placements[i].y;
        w = (float)placements[i].w;
        h = (float)placements[i].h;
        regions[items[i].id] = (zcsr_atlas_region){
            x / aw, y / ah, (x + w) / aw, (y + h) / ah
        };
    }
}

zcsr_atlas_region zcsr_atlas_lookup(const zcsr_atlas_region* regions, uint32_t id) {
    return regions[id];
}

void zcsr_atlas_blit(uint8_t* atlas_pixels, uint16_t atlas_w,
                     int x, int y,
                     const uint8_t* src, uint16_t src_w, uint16_t src_h) {
    int row;
    for (row = 0; row < src_h; ++row) {
        const uint8_t* s = src + (size_t)row * src_w * 4u;
        uint8_t* d = atlas_pixels + ((size_t)(y + row) * atlas_w + x) * 4u;
        int col;
        for (col = 0; col < src_w * 4; ++col) d[col] = s[col];
    }
}
