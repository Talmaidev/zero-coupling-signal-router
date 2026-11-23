#ifndef ZCSR_SCENE_SUBMIT_H
#define ZCSR_SCENE_SUBMIT_H

/* Unified scene submission bridge.
 *
 * One caller-facing entry decides how a zcsr_scene reaches the renderer:
 *   DISPLAY -> screen-space quads (path A, unlit, painter order);
 *   LIT     -> world-space faces through the Light2D G-buffer (path B).
 *
 * The bridge owns no scene data and no GL state. It reuses two canonical
 * sources instead of duplicating them:
 *   - zcsr_scene_entity_quad() for the entity shape (pivot/rotation/override);
 *   - zcsr_scene_draw_quads() for the display path.
 *
 * Axis adapter: zcsr_scene is x/z ground + y height; Light2D is x/y ground +
 * z elevation. Therefore scene (x,y,z) -> Light2D (x, z, y).
 *
 * Camera adapter is exact for the supported slice (yaw=0, roll=0, pitch in
 * (0,90]). The 2.5D shear camera has one isotropic ppu, so a pitched ortho
 * camera's depth foreshortening is approximated; horizontal scale is exact.
 * Non-zero yaw/roll are rejected rather than silently mis-projected.
 */

#include "zcsr/camera.h"
#include "zcsr/gl_render.h"
#include "zcsr/scene.h"
#include "zcsr/scene_render.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum zcsr_scene_submit_path {
    ZCSR_SCENE_SUBMIT_DISPLAY = 0, /* path A: screen-space, unlit */
    ZCSR_SCENE_SUBMIT_LIT = 1      /* path B: world G-buffer faces */
} zcsr_scene_submit_path;

/* One scene entity in Light2D world axes. corners_xyz is four points in the
 * scene's boundary order; normal_xyz follows that boundary order (consistent
 * winding required: reversing the four points flips the normal), so explicit
 * quads work too. tint is the scene's display-domain tint. */
typedef struct zcsr_scene_world_quad {
    zcsr_entity_id id;
    float corners_xyz[12];
    float normal_xyz[3];
    float tint_rgba[4];
    uint32_t texture;
} zcsr_scene_world_quad;

typedef struct zcsr_scene_submit_stats {
    size_t visible;   /* visible entities, including lights */
    size_t drawable;  /* visible, non-light entities with a valid canonical quad */
    size_t submitted; /* faces/quads accepted by the renderer */
    size_t skipped;   /* visible - submitted (lights, invalid, or renderer-rejected) */
} zcsr_scene_submit_stats;

/* Convert a scene camera to the Light2D shear camera. Returns false for an
 * unsupported camera (non-zero yaw/roll, non-positive pitch/ortho/view, or a
 * degenerate cotangent). Pure math, no GL. */
bool zcsr_scene_camera_to_camera(const zcsr_scene_camera* scene_camera,
                                 zcsr_camera* out_camera);

/* Build the canonical world quad for one entity, converting scene axes to
 * Light2D axes. Returns false when the entity is missing or the resulting
 * quad/normal is non-finite. Pure math, no GL. */
bool zcsr_scene_entity_world_quad(const zcsr_scene* scene,
                                  zcsr_entity_id id,
                                  zcsr_scene_world_quad* out_quad);

/* Path-B submission order: visible, non-light entities with a valid canonical
 * quad, sorted by the shear-camera q key ascending (far-to-near), then
 * insertion index. Path B has no depth attachment, so layer/order are NOT
 * depth keys; they remain Path-A painter controls only. q is averaged over
 * the four face corners:
 *   q = camera->height_shear * (light2d_world_y - camera->center_y)
 *       + light2d_world_z
 * where light2d_world_y is scene z (ground depth) and light2d_world_z is
 * scene y (height). This is the only LIT sort used by zcsr_scene_submit.
 * Pure math, no GL. Returns the number of indices written (<= capacity); 0 on
 * invalid input or no drawable entities. */
size_t zcsr_scene_build_lit_order(const zcsr_scene* scene,
                                  const zcsr_camera* camera,
                                  size_t* out_indices,
                                  size_t capacity);

/* Submit the whole scene through one path.
 *   DISPLAY requires scratch/capacity >= drawable count (visible non-light
 *   entities with a valid canonical quad; lights are skipped). It performs its
 *   own begin/flush through zcsr_scene_draw_quads and succeeds only when
 *   submitted == drawable.
 *   LIT ignores scratch/capacity, sorts faces by the documented q key, and
 *   performs geo_begin/geo_end. geo_begin fails unless the current GL
 *   framebuffer is a valid four-attachment Light2D G-buffer bound through
 *   zcsr_light2d_begin_geometry / zcsr_gl_l2d_gbuffer_begin. LIT also succeeds
 *   only when submitted == drawable.
 * Returns false on invalid input, unsupported camera, insufficient display
 * scratch, unbound/invalid LIT G-buffer, or a renderer begin/end/face failure.
 * Stats may be NULL; when present they are zeroed first and filled on both
 * success and failure. */
bool zcsr_scene_submit(zcsr_gl_renderer* renderer,
                       const zcsr_scene* scene,
                       zcsr_scene_submit_path path,
                       zcsr_scene_quad_instance* scratch,
                       size_t capacity,
                       zcsr_scene_submit_stats* out_stats);

#endif /* ZCSR_SCENE_SUBMIT_H */
