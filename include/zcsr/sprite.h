#ifndef ZCSR_SPRITE_H
#define ZCSR_SPRITE_H
/* Agent 3 (Game Core) — batch sprite renderer. Submit many sprites, draw them in one flush.
 * Target: render n*10000 (n>0) sprites within the frame budget; bench gate = 200 sprites <1ms.
 * Implementer: modules/render. Draws onto the platform surface. */
#include "texture.h"
#include "platform.h"
#include <stddef.h>

typedef struct { float x, y, w, h; } zcsr_rectf; /* destination rect, top-left origin */

typedef struct {
    const zcsr_texture* tex;
    zcsr_rectf          dst;
    /* (uv sub-rect / tint can be added later without breaking ABI) */
} zcsr_sprite;

typedef struct zcsr_sprite_batch zcsr_sprite_batch; /* opaque; fixed capacity in caller buffer */

zcsr_sprite_batch* zcsr_sprite_batch_init(void* buffer, size_t bytes, zcsr_surface* target);
void               zcsr_sprite_batch_begin(zcsr_sprite_batch*);
void               zcsr_sprite_batch_submit(zcsr_sprite_batch*, const zcsr_sprite*);
void               zcsr_sprite_batch_flush(zcsr_sprite_batch*); /* draw all submitted in one batch */

/* ---- GL sprite component (camera-aware; drives glrender, additive) ------------------
 * NOTE: the instance type is zcsr_sprite_instance — the plain zcsr_sprite name
 * above already belongs to the software-render contract and must not change.
 *
 * glrender's public types are redeclared identically here (C11 permits exact
 * typedef repeats) so this section stands alone no matter which header a
 * translation unit includes first; gl_render.h includes this file for
 * zcsr_rectf. */
#include <stdint.h>
#include "camera.h"
#include "transform.h"

typedef struct zcsr_gl_renderer zcsr_gl_renderer;
typedef uint32_t zcsr_gl_texture;

/* Shared sprite resource (many entities can reference one asset). */
typedef struct {
    zcsr_gl_texture tex;
    float u0, v0, u1, v1;   /* atlas UV sub-rect; 0,0,1,1 = whole texture */
    float w, h;             /* world-space size at scale 1 */
} zcsr_sprite_asset;

/* Sprite instance on an entity. asset == NULL (or tex == 0) draws nothing. */
typedef struct {
    const zcsr_sprite_asset* asset;
    float r, g, b, a;        /* tint; 1,1,1,1 = original */
} zcsr_sprite_instance;

/* Enqueue one sprite into the renderer's current batch: world size and center
 * project through the camera, rotation comes from the transform. Enqueue-only
 * — the caller owns begin/flush. Rotation is applied on the whole-texture
 * path; atlas sub-rects enqueue without rotation (the atlas queue has no
 * rotation channel). */
void zcsr_sprite_draw(zcsr_gl_renderer* r,
                      const zcsr_camera* cam,
                      const zcsr_transform* tr,
                      const zcsr_sprite_instance* sp);

/* Lit sprite (Light2D path B): submit a transform + sprite_instance as a
 * world-space textured quad into the G-buffer. Caller must already be inside
 * an open light2d geometry phase (after zcsr_gl_l2d_geo_begin, before geo_end).
 * ground_z is the world height of the sprite plane (0 = ground).
 * See RENDER2D_CONTRACT §14.4 for the corner/uv/normal convention. */
void zcsr_sprite_draw_lit(zcsr_gl_renderer* r,
                          const zcsr_transform* tr,
                          const zcsr_sprite_instance* sp,
                          float ground_z);

#endif /* ZCSR_SPRITE_H */
