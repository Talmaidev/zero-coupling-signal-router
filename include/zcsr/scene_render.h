#ifndef ZCSR_SCENE_RENDER_H
#define ZCSR_SCENE_RENDER_H

/* 2.5D scene render bridge.
 *
 * Converts zcsr_scene entities into two explicit submission forms:
 *   - zcsr_gl_instance rectangles in stable scene order (fast rectangle path);
 *   - zcsr_scene_quad_instance four-corner quads in stable scene order.
 * Neither form is a 3D renderer. The rectangle path carries one rotation;
 * the quad path carries four already-projected screen corners. Callers submit
 * in array order and must not run opaque texture sorting on the scene order.
 */

#include "zcsr/gl_render.h"
#include "zcsr/scene.h"

#include <stddef.h>
#include <stdint.h>

/* One projected scene quad. The four screen-space corners are already in
 * output pixels; the renderer does not rotate or expand an axis-aligned
 * rectangle. `u`/`v` are the four matching UV corners. The scene list owns
 * stable order; submit instances in array order. */
typedef struct zcsr_scene_quad_instance {
    zcsr_entity_id id;
    float x[4], y[4];          /* screen pixels, same corner order as scene */
    float u[4], v[4];
    float depth;               /* average forward depth; painter hint only */
    float r, g, b, a;
    uint32_t texture;
} zcsr_scene_quad_instance;

/* Fill `out` with visible entities in stable scene order. Returns the number
 * written. 0 means invalid input or no visible entity. `out` is caller-owned. */
size_t zcsr_scene_build_instances(const zcsr_scene* scene,
                                  zcsr_gl_instance* out, size_t capacity);

/* Project every visible entity to an explicit four-corner quad. This is the
 * arbitrary-quad bridge; it does not replace the rectangle bridge above.
 * Returns the number written. 0 means invalid input or no visible entity. */
size_t zcsr_scene_build_quad_instances(const zcsr_scene* scene,
                                       zcsr_scene_quad_instance* out,
                                       size_t capacity);

/* Build quad instances into `scratch`, submit them through the arbitrary-quad
 * GL path, and flush. `scratch` is caller-owned and must hold at least
 * `capacity` entries. Returns the number of scene quads submitted. Does not
 * present the surface, does not sort textures, and does not run a 3D pipeline.
 * Texture handle 0 uses the renderer's white texture. */
size_t zcsr_scene_draw_quads(zcsr_gl_renderer* renderer, const zcsr_scene* scene,
                             zcsr_scene_quad_instance* scratch, size_t capacity);

#endif /* ZCSR_SCENE_RENDER_H */
