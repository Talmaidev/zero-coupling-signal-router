#ifndef ZCSR_LIGHT2D_H
#define ZCSR_LIGHT2D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zcsr/gl_render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zcsr_light2d_ctx zcsr_light2d_ctx;

typedef enum zcsr_light_quality {
    ZCSR_LIGHT_QUALITY_LOW = 0,
    ZCSR_LIGHT_QUALITY_MEDIUM = 1,
    ZCSR_LIGHT_QUALITY_HIGH = 2,
    ZCSR_LIGHT_QUALITY_ULTRA = 3
} zcsr_light_quality;

typedef enum zcsr_light_type {
    ZCSR_LIGHT_POINT = 0,     /* radial falloff from a world position */
    ZCSR_LIGHT_PARALLEL = 1   /* fixed travel direction, no falloff */
} zcsr_light_type;

/*
 * Lights live in WORLD units (see zcsr_camera in camera.h): x/y locate the light
 * on the ground plane, radius/decay measure falloff in world units, and
 * `height` is the light's elevation above the plane: up-facing surfaces are
 * lit only through this elevation (a ground-level light leaves them dark),
 * in-plane wall normals catch horizontal rays full-strength.
 * PARALLEL lights instead travel along the normalized (dir_x, dir_y, dir_z)
 * direction (like the reference engine's rotated (0,-1,1).unit): no distance
 * falloff, shadow extrusion follows the direction's in-plane projection, and
 * radius/decay/position are ignored. Parallel lights need HIGH quality; the
 * LOW fallback skips them.
 * RGB are linear-light colors and `a` is the per-light intensity multiplier
 * (color rgb * a drives the pass, like the reference engine's color.w x gain);
 * values may exceed 1.0 for HDR-style inputs, provided they remain finite
 * and non-negative.
 */
typedef struct zcsr_light {
    float x;
    float y;
    float height;
    float radius;
    float decay;
    float dir_x, dir_y, dir_z; /* parallel: normalized travel direction */
    zcsr_light_type type;
    float r;
    float g;
    float b;
    float a;
} zcsr_light;

/* ---- Camera (world-unit orthographic, SHARED) -----------------------------------------
 * The light2d pipeline projects through the caller's zcsr_camera (camera.h):
 * set_camera stores the POINTER (not a copy), so pan/zoom changes apply the
 * next frame without re-setting — the caller's struct is the single source
 * of truth and must stay alive until the next set_camera or destroy.
 * light2d reads only center_x/center_y/px_per_unit from it; the viewport is
 * ALWAYS the context's own transactionally-resized width/height, so the
 * camera's view fields never override the render target. Callers that never
 * set a camera run on the built-in default: origin centered, 128 px per
 * unit (one 128px reference tile). Occluders (below) are registered in the
 * same world units. */
#include "camera.h"

bool zcsr_light2d_set_camera(zcsr_light2d_ctx* ctx, const zcsr_camera* camera);
void zcsr_light2d_get_camera(const zcsr_light2d_ctx* ctx, zcsr_camera* out);

/* Debug/introspection: the G-buffer render target (HIGH quality only), for
 * caller-side channel inspectors drawn after end_lighting — note that
 * end_lighting pops the pipeline's saved GL state, so re-bind the target's
 * textures (zcsr_gl_l2d_bind_target_textures) before sampling. Invalid
 * handle when the context has no G-buffer (LOW quality / create failed). */
#include "gl_render_light2d_ext.h"
zcsr_gl_l2d_target zcsr_light2d_gbuffer_handle(const zcsr_light2d_ctx* ctx);

/*
 * `max_quality` reserves caller-buffer capacity only and may be MEDIUM or ULTRA
 * to reserve the larger light-slot capacity (LOW 16 / MEDIUM 32 / HIGH and
 * ULTRA 64); those strategies are not implemented. Only LOW and HIGH are
 * implemented: `initial_quality` and set_quality() accept LOW/HIGH and reject
 * MEDIUM/ULTRA.
 * The caller buffer must remain alive and exclusively owned by this context
 * until zcsr_light2d_destroy() returns.
 */
size_t zcsr_light2d_buffer_bytes_required(zcsr_light_quality max_quality);

zcsr_light2d_ctx* zcsr_light2d_create(zcsr_gl_renderer* renderer,
                                      zcsr_light_quality initial_quality,
                                      zcsr_light_quality max_quality,
                                      int width,
                                      int height,
                                      void* buffer,
                                      size_t bytes);

void zcsr_light2d_destroy(zcsr_light2d_ctx* ctx);

/*
 * resize is valid only between frames. The renderer's target-resize operation
 * must be transactional: on failure the previous target remains usable.
 */
bool zcsr_light2d_resize(zcsr_light2d_ctx* ctx, int width, int height);

/* Runtime strategy switching is valid only between frames. Unsupported
 * MEDIUM/ULTRA requests return false and leave the active quality unchanged. */
bool zcsr_light2d_set_quality(zcsr_light2d_ctx* ctx,
                              zcsr_light_quality quality);

/* Frame sequence: begin_geometry -> caller draws scene geometry ->
 * begin_lighting -> add_light zero or more times -> end_lighting.
 * LOW leaves the existing geometry target untouched. HIGH binds a four-target
 * G-buffer; caller geometry shaders must write the attachment contract listed
 * in gl_render_light2d_ext.h. */
bool zcsr_light2d_begin_geometry(zcsr_light2d_ctx* ctx);
bool zcsr_light2d_begin_lighting(zcsr_light2d_ctx* ctx);
bool zcsr_light2d_add_light(zcsr_light2d_ctx* ctx, const zcsr_light* light);
bool zcsr_light2d_end_lighting(zcsr_light2d_ctx* ctx);

/* Optional explicit clear while the context is between frames. begin_geometry
 * also clears the list automatically. */
bool zcsr_light2d_clear_lights(zcsr_light2d_ctx* ctx);

/* ---- HIGH pipeline runtime options (between-frames only, like set_quality) ----------
 * shadows: render a per-light shadow mask from the registered occluders (default on).
 * normals: apply the 2D N.L term (default on).
 * gain: light gain multiplier, >= 0 (default 1.0).
 * light_scale_pct: light/shadow buffer scale in percent, one of 25/50/100 (default 50).
 * bloom: composite through the bloom variant — 9-tap brightness-gated blur of
 *   the light and emissive buffers before the tonemap (default off).
 * ray_shadows: per-pixel segment-vs-prism occlusion in the light shader
 *   (Geometry Proof v1 semantics — occluder HEIGHT shortens/lengthens shadows);
 *   each registered occluder is uploaded as its footprint AABB + height, the
 *   2D extrusion mask is bypassed. Point lights only (default off). */
typedef struct {
    bool shadows;
    bool normals;
    float gain;
    int light_scale_pct;
    bool bloom;
    bool ray_shadows;
} zcsr_light2d_options;

bool zcsr_light2d_set_options(zcsr_light2d_ctx* ctx, const zcsr_light2d_options* options);
void zcsr_light2d_get_options(const zcsr_light2d_ctx* ctx, zcsr_light2d_options* out);

/* ---- Occluders: convex polygons in the same WORLD units as lights --------------------
 * For each shadow-casting light the pipeline extrudes the selected occluder
 * edges away from the light into a mask RT (MAX-blended); the light pass
 * multiplies by (1 - mask). Per-occluder attributes (reference engine's
 * Block/Wall/SelfShadow semantics):
 *   height      top elevation; auto_disable/self_shadow compare it to the
 *               light's height,
 *   factor      shadow density (0..1],
 *   distance    extrusion length in world units; <= 0 = infinite (the
 *               default wedge), finite values give bounded wall-style bands,
 *   reverse     extrude the opposite edges toward the light,
 *   self_shadow fill the polygon itself into the mask instead of extruding
 *               (covered while the light is ABOVE the occluder's height,
 *               or always when auto_disable is false),
 *   auto_disable skip the occluder entirely while the light is at or below
 *               its height (an at/above-light block is lit, casting nothing).
 * Fixed capacity: at most ZCSR_LIGHT2D_MAX_OCCLUDERS polygons of at most
 * ZCSR_LIGHT2D_MAX_OCCLUDER_VERTS vertices; overflow and non-finite input
 * are rejected with false. Valid between frames only. */
#define ZCSR_LIGHT2D_MAX_OCCLUDERS 64
#define ZCSR_LIGHT2D_MAX_OCCLUDER_VERTS 16
typedef struct {
    const float* xy;      /* world units, convex order */
    size_t count;         /* 3..ZCSR_LIGHT2D_MAX_OCCLUDER_VERTS */
    float height;         /* top elevation, world units */
    float factor;         /* shadow density (0..1] */
    float distance;       /* extrusion length, world units; <= 0 = infinite */
    bool reverse;         /* extrude toward the light */
    bool self_shadow;     /* fill the polygon itself, no extrusion */
    bool auto_disable;    /* no shadow while the light is at/below `height` */
} zcsr_light2d_occluder;
bool zcsr_light2d_add_occluder(zcsr_light2d_ctx* ctx, const zcsr_light2d_occluder* occluder);
void zcsr_light2d_clear_occluders(zcsr_light2d_ctx* ctx);

#ifdef __cplusplus
}
#endif
#endif
