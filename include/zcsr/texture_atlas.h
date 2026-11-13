#ifndef ZCSR_TEXTURE_ATLAS_H
#define ZCSR_TEXTURE_ATLAS_H
/* Texture atlas — shelf-packing of many small images into one large texture.
 *
 * WHY: binding a different texture per sprite breaks batch merging. Packing all small
 * images into one atlas lets every sprite share a single bound texture (only the UV
 * region differs), so the whole batch collapses into one draw call.
 *
 * This header is pure packing logic (no GL). The atlas pixels are composed on the CPU
 * into a caller buffer, then uploaded once via zcsr_gl_texture_create. No runtime heap:
 * all item/placement/region arrays are caller-provided.
 */
#include <stdint.h>
#include <stddef.h>

/* One small image to pack. The packer reorders this array in place (sorts by height),
 * so it must be writable. */
typedef struct {
    uint32_t id;    /* caller-assigned texture id; used as the lookup key / region index */
    uint16_t w, h;  /* image size in pixels */
} zcsr_atlas_item;

/* Where one small image landed in the atlas (pixel coordinates, top-left origin).
 * x<0 means it did not fit (pack failed for that item). */
typedef struct {
    int16_t  x, y;
    uint16_t w, h;
} zcsr_atlas_placement;

/* Normalized UV sub-rectangle of one image inside the atlas. */
typedef struct {
    float u0, v0, u1, v1;
} zcsr_atlas_region;

/* Shelf-pack (next-fit decreasing height) `count` items into an atlas of at most
 * max_w x max_h pixels. A 1px gutter is reserved between images to avoid bilinear bleed.
 * `items` is reordered in place (height descending). `placements` is written in the SAME
 * post-sort order as items (index-aligned), not by id.
 *
 * Returns the number of items successfully placed; a value < count means the atlas ran
 * out of room. On success, *used_w / used_h receive the tight atlas size actually used. */
uint32_t zcsr_atlas_pack(zcsr_atlas_item* items, uint32_t count,
                         uint16_t max_w, uint16_t max_h,
                         zcsr_atlas_placement* placements,
                         uint16_t* used_w, uint16_t* used_h);

/* Build a per-id region table: regions[item.id] = normalized UV of that item's placement.
 * Call after pack. `regions` must hold at least (max id + 1) entries. */
void zcsr_atlas_build_regions(const zcsr_atlas_item* items,
                              const zcsr_atlas_placement* placements, uint32_t count,
                              uint16_t atlas_w, uint16_t atlas_h,
                              zcsr_atlas_region* regions);

/* Region lookup by id. Returns a zero region for an out-of-range id. */
zcsr_atlas_region zcsr_atlas_lookup(const zcsr_atlas_region* regions, uint32_t id);

/* Copy one RGBA8 image (src, src_w x src_h) into the atlas pixel buffer at (x,y).
 * The atlas is atlas_w pixels wide, tightly packed RGBA8 rows. Used by the caller to
 * compose the full atlas after packing. */
void zcsr_atlas_blit(uint8_t* atlas_pixels, uint16_t atlas_w,
                     int x, int y,
                     const uint8_t* src, uint16_t src_w, uint16_t src_h);

#endif /* ZCSR_TEXTURE_ATLAS_H */
