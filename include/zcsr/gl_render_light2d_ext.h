#ifndef ZCSR_GL_RENDER_LIGHT2D_EXT_H
#define ZCSR_GL_RENDER_LIGHT2D_EXT_H

/*
 * Additive renderer contract required by modules/light2d/light2d.c.
 * These declarations are new APIs, not claims about the current repository.
 * Implement them inside modules/glrender and expose this header through the
 * normal include/zcsr contract boundary. No platform GL header is exposed.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "zcsr/gl_render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t zcsr_gl_l2d_target;
#define ZCSR_GL_L2D_TARGET_INVALID ((zcsr_gl_l2d_target)0)

typedef enum zcsr_gl_l2d_format {
    ZCSR_GL_L2D_FORMAT_RGBA8 = 1,
    ZCSR_GL_L2D_FORMAT_RGBA16F = 2
} zcsr_gl_l2d_format;

typedef enum zcsr_gl_l2d_program {
    ZCSR_GL_L2D_PROGRAM_RADIAL = 0,
    ZCSR_GL_L2D_PROGRAM_DEFERRED = 1,
    /* Composite variant: 9-tap brightness-gated bloom of the light and
     * emissive buffers before the same tonemap (u_LightRes = light-buffer
     * size must be set while this program is bound). */
    ZCSR_GL_L2D_PROGRAM_DEFERRED_BLOOM = 2
} zcsr_gl_l2d_program;

typedef enum zcsr_gl_l2d_blend_mode {
    ZCSR_GL_L2D_BLEND_OPAQUE = 0,
    ZCSR_GL_L2D_BLEND_ADDITIVE = 1
} zcsr_gl_l2d_blend_mode;

/* Save/restore all renderer state touched by this extension: target, viewport,
 * scissor, active program, texture bindings and blend state. The stack is
 * renderer-owned; implementations must fail cleanly if it has no capacity. */
bool zcsr_gl_l2d_state_push(zcsr_gl_renderer* renderer);
bool zcsr_gl_l2d_state_pop(zcsr_gl_renderer* renderer);

/* Create/destroy/resize an MRT target. Resize must be transactional. The
 * requested attachments are ordered: Albedo, Position, Normal, Emissive. */
bool zcsr_gl_l2d_target_create(zcsr_gl_renderer* renderer,
                               int width,
                               int height,
                               const zcsr_gl_l2d_format* formats,
                               size_t attachment_count,
                               zcsr_gl_l2d_target* out_target);
bool zcsr_gl_l2d_target_resize(zcsr_gl_renderer* renderer,
                               zcsr_gl_l2d_target target,
                               int width,
                               int height);
void zcsr_gl_l2d_target_destroy(zcsr_gl_renderer* renderer,
                                zcsr_gl_l2d_target target);

/* Bind and clear the G-buffer. Clear values must encode an empty albedo and
 * emissive as zero, position as a FAR sentinel (air receives no light — the
 * reference engine clears its view-space position channel to zfar), and the
 * 2D neutral normal as (0.5,0.5).
 * Geometry output layout: location 0 Albedo RGBA8; location 1 Position RGBA16F
 * with xyz in WORLD 3D coordinates (x,y ground plane, z elevation; painter
 * converts its pixel-space geometry through the camera); location 2 Normal
 * RGBA8 with xy encoded from [-1,+1] to [0,1] ((0,0) decodes to the UP
 * normal, other values to in-plane wall normals); location 3 Emissive RGBA8. */
bool zcsr_gl_l2d_gbuffer_begin(zcsr_gl_renderer* renderer,
                               zcsr_gl_l2d_target target,
                               int width,
                               int height);
bool zcsr_gl_l2d_bind_default_target(zcsr_gl_renderer* renderer,
                                    int width,
                                    int height);
bool zcsr_gl_l2d_bind_target_textures(zcsr_gl_renderer* renderer,
                                      zcsr_gl_l2d_target target,
                                      int first_texture_unit);

/* These program IDs select renderer-managed programs compiled from
 * shaders/light2d_low.frag and shaders/light2d.frag plus the renderer's
 * compatible fullscreen/quad vertex shader. */
bool zcsr_gl_l2d_set_program(zcsr_gl_renderer* renderer,
                             zcsr_gl_l2d_program program);
bool zcsr_gl_l2d_uniform_1f(zcsr_gl_renderer* renderer,
                            const char* name, float x);
bool zcsr_gl_l2d_uniform_2f(zcsr_gl_renderer* renderer,
                            const char* name, float x, float y);
bool zcsr_gl_l2d_uniform_3f(zcsr_gl_renderer* renderer,
                            const char* name, float x, float y, float z);
bool zcsr_gl_l2d_uniform_4f(zcsr_gl_renderer* renderer,
                            const char* name, float x, float y,
                            float z, float w);
bool zcsr_gl_l2d_uniform_1i(zcsr_gl_renderer* renderer,
                            const char* name, int value);
bool zcsr_gl_l2d_uniform_vec4_array(zcsr_gl_renderer* renderer,
                                    const char* name,
                                    const float* values,
                                    size_t vec4_count);
bool zcsr_gl_l2d_set_blend_mode(zcsr_gl_renderer* renderer,
                                zcsr_gl_l2d_blend_mode mode);
bool zcsr_gl_l2d_draw_fullscreen_quad(zcsr_gl_renderer* renderer);
/* Draw screen-space rect with UV [0,1] in the renderer's normal 2D convention. */
bool zcsr_gl_l2d_draw_quad(zcsr_gl_renderer* renderer,
                           float x, float y, float width, float height);

/* ---- Light2d v2 passes (JoyECS-style shadow mask + light accumulation) ---------------
 * Sequence per frame inside end_lighting:
 *   for each shadow-casting light:
 *     shadow_begin(...) -> shadow_edge(...)/shadow_fill(...) per occluder
 *     -> shadow_end()
 *   lightbuf_begin(...) -> light_emit(...) per light -> lightbuf_end()
 *   then bind_default_target + bind_target_textures + deferred composite.
 * shadow_begin clears the R mask to 0 and MAX-blends the occluder geometry:
 * point lights extrude selected edges radially away from (src_x, src_y);
 * parallel lights extrude along the fixed unit direction (dir_x, dir_y);
 * distance_px bounds the extrusion (finite wall-style bands), reverse edges
 * extrude toward the light instead. shadow_fill covers a convex polygon
 * itself (self-shadow), no extrusion. lightbuf_begin clears the RGBA16F
 * accumulation target; light_emit adds per light: POINT = one radial falloff
 * quad pow(1-r/R,decay) * 1/(dist3/DIST_SCALE+1); PARALLEL = fullscreen, no
 * falloff, direction-driven. Both are multiplied by the PURE max(0, N.L3)
 * term (optional), (1 - shadow) and gain. N.L3 uses the world light position
 * elevated by `height` (point) or the fixed direction (parallel); the neutral
 * G-buffer normal (0,0) decodes to the up normal (0,0,1), any other value to
 * an in-plane wall normal. Samples the G-buffer normal (unit 6) and that
 * light's shadow mask (unit 8, 5-tap MAX dilate). */
bool zcsr_gl_l2d_shadow_begin(zcsr_gl_renderer* renderer,
                              int mask_w, int mask_h,
                              int view_w, int view_h,
                              float src_x, float src_y,
                              float dir_x, float dir_y,
                              float distance_px,
                              int parallel);
bool zcsr_gl_l2d_shadow_edge(zcsr_gl_renderer* renderer,
                             float ax, float ay, float bx, float by,
                             float factor, int reverse, float distance_px);
bool zcsr_gl_l2d_shadow_fill(zcsr_gl_renderer* renderer,
                             const float* xy_px, size_t count, float factor);
bool zcsr_gl_l2d_shadow_end(zcsr_gl_renderer* renderer);

/* px_per_unit is the camera zoom (pixels per world unit): the accumulation
 * quads are sized in pixels from world-unit light radii through it. */
bool zcsr_gl_l2d_lightbuf_begin(zcsr_gl_renderer* renderer,
                                int buf_w, int buf_h,
                                int view_w, int view_h,
                                float gain, float dist_scale,
                                float px_per_unit,
                                int normals_on);
/* One accumulated light. quad_x/quad_y = the light center in SCREEN pixels
 * (caller projects through its camera, point lights only); wx/wy/height and
 * radius/decay are WORLD units. parallel != 0 switches to the reference
 * engine's parallel light: no falloff, fullscreen coverage, the normalized
 * 3D travel direction (dir_x, dir_y, dir_z) drives both N.L and the shadow
 * extrusion; radius/decay/position are ignored there. cr/cg/cb should
 * already carry the per-light intensity (color * a). The N.L term is the
 * reference engine's PURE max(0, dot) in 3D: up-facing surfaces (neutral
 * G-buffer normal) are lit only through the light's elevation/direction's z,
 * in-plane wall normals are struck full-strength by horizontal rays. The
 * shadow mask is sampled with a 5-tap MAX dilate. */
typedef struct zcsr_gl_l2d_light_draw {
    float quad_x, quad_y;
    float wx, wy, height;
    float radius, decay;
    float dir_x, dir_y, dir_z;
    int   parallel;
    float cr, cg, cb;
    int   use_shadow;
} zcsr_gl_l2d_light_draw;

bool zcsr_gl_l2d_light_emit(zcsr_gl_renderer* renderer,
                            const zcsr_gl_l2d_light_draw* light);
bool zcsr_gl_l2d_lightbuf_end(zcsr_gl_renderer* renderer);

/* Binds the light accumulation texture on the unit right after the G-buffer block
 * (composite sampler u_Light). */
bool zcsr_gl_l2d_bind_light_texture(zcsr_gl_renderer* renderer,
                                    int texture_unit);

#ifdef __cplusplus
}
#endif
#endif
