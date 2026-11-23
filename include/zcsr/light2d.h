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
    ZCSR_LIGHT_PARALLEL = 1,  /* fixed travel direction, no falloff */
    ZCSR_LIGHT_SPOT = 2       /* point light restricted to a cone */
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
    float dir_x, dir_y, dir_z; /* parallel: normalized travel direction;
                                  spot: normalized cone aim direction */
    float inner_angle;         /* spot: full cone radius (radians) at full power */
    float outer_angle;         /* spot: full cone radius (radians) at zero power */
    zcsr_light_type type;
    float r;
    float g;
    float b;
    float a;
} zcsr_light;

/* ---- Scene environment: ambient + sun (first-class parallel light) ----------
 * The sun is NOT another local point light. It is one scene-wide parallel
 * light owned by the environment, has no position/radius/decay, and is
 * submitted before local lights. dir_* is the normalized direction of travel
 * (sunlight travels DOWN: dir_z < 0). RGB is linear-light color, `intensity`
 * scales it (0 = disabled). cast_shadow enables the parallel shadow mask;
 * shadow_length_scale multiplies the geometric shadow length derived from
 * dir_z (1 = physically consistent, 0.5 = half length).
 *
 * ambient_* is linear ambient radiance. exposure is a positive linear scaling
 * applied before the x/(x+1) tonemap. ambient_occlusion is a global [0,1]
 * multiplier on ambient only (1 = no occlusion); it never darkens direct
 * light. A per-pixel AO mask is not part of this contract yet.
 *
 * QUALITY: HIGH/ULTRA consume the sun. LOW/MEDIUM are the forward path and
 * ignore the sun (explicitly, not silently): the forward shader has no
 * direction term. */
typedef struct zcsr_light2d_sun {
    bool enabled;
    bool cast_shadow;
    float dir_x, dir_y, dir_z;
    float r, g, b;
    float intensity;
    float shadow_length_scale;
} zcsr_light2d_sun;

typedef struct zcsr_light2d_environment {
    float ambient_r, ambient_g, ambient_b;
    float exposure;
    float ambient_occlusion;
    zcsr_light2d_sun sun;
} zcsr_light2d_environment;

/* Fill *out with the historical defaults: ambient 0.03, exposure 1, AO off,
 * sun disabled (travel direction (0,0.5,-1) normalized by the validator). */
void zcsr_light2d_environment_default(zcsr_light2d_environment* out);

/* Validate every field (finite, non-negative, normalized direction, ranges).
 * Does not mutate. Returns false on any invalid field. */
bool zcsr_light2d_environment_is_valid(const zcsr_light2d_environment* env);

/* Convert an enabled sun to the parallel-light record consumed by the
 * pipeline. Returns false for a disabled or invalid sun. The output `a`
 * carries intensity, type is ZCSR_LIGHT_PARALLEL, and x/y/height/radius/decay
 * are zero. */
bool zcsr_light2d_sun_to_light(const zcsr_light2d_sun* sun, zcsr_light* out);

/* Geometric ground-shadow length for a parallel light, in pixels:
 *
 *   height * |dir_xy| / |dir_z| * shadow_length_scale * ppu
 *
 * dir_xy is (dir_x, dir_y); the direction may be non-normalized. Returns 0
 * for a non-positive height/scale/ppu, and 100000 px when |dir_z| is within
 * 1e-6 of zero (the light is horizontal and the band is unbounded). This is
 * pure math shared by the light2d occluder emitter and unit tests; it does
 * not clamp to the scene. */
float zcsr_light2d_parallel_shadow_length_px(float occluder_height,
                                              float dir_x,
                                              float dir_y,
                                              float dir_z,
                                              float shadow_length_scale,
                                              float ppu);

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
 * 四档全部可切换（LOW / MEDIUM / HIGH / ULTRA），映射到两条真实管线：
 *   LOW（16 槽）/ MEDIUM（32 槽）  → forward 叠加路径，不建 G-buffer；
 *   HIGH（64 槽）/ ULTRA（64 槽）  → G-buffer deferred 路径。
 * 同一路径内的两档差异是槽容量与光/影缓冲分辨率：切到 ULTRA 时
 * light_scale_pct 默认提到 100、bloom 默认开（之后可用 set_options 覆盖）。
 * `max_quality` 决定 caller 缓冲容量：LOW 16 / MEDIUM 32 / HIGH 64 / ULTRA 64。
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

/* Runtime strategy switching is valid only between frames. Requests above the
 * context's max_quality, or HIGH/ULTRA without a G-buffer, return false and
 * leave the active quality unchanged. */
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

/* Replace the scene environment. Valid between frames only. Invalid values
 * (non-finite, negative RGB/intensity, non-normalized sun direction, exposure
 * <= 0, ambient_occlusion outside [0,1]) are rejected and leave the previous
 * environment intact. HIGH/ULTRA use every field; LOW/MEDIUM ignore the sun.
 * Defaults are zcsr_light2d_environment_default(). */
bool zcsr_light2d_set_environment(zcsr_light2d_ctx* ctx,
                                  const zcsr_light2d_environment* environment);
void zcsr_light2d_get_environment(const zcsr_light2d_ctx* ctx,
                                  zcsr_light2d_environment* out);

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
