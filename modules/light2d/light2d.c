#include "zcsr/light2d.h"
#include "zcsr/gl_render_light2d_ext.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>

#define ZCSR_L2D_MAX_LIGHTS 64u
#define ZCSR_L2D_MEDIUM_LIGHTS 32u
#define ZCSR_L2D_LOW_LIGHTS 16u
#define ZCSR_L2D_FIRST_TEXTURE_UNIT 4

/* MSVC's C mode does not define max_align_t in <stddef.h> under strict /std:c11; derive the
 * practical maximum alignment from fundamental types instead (the context stores only
 * pointers, floats and integers, which need at most this alignment). */
typedef union zcsr_l2d_align_probe {
    double d;
    unsigned long long u;
    void* p;
} zcsr_l2d_align_probe;
#define ZCSR_L2D_MAX_ALIGN _Alignof(zcsr_l2d_align_probe)

/* Public contract order: Albedo, Position, Normal, Emissive (HDR). */
static const zcsr_gl_l2d_format zcsr_l2d_gbuffer_formats[4] = {
    ZCSR_GL_L2D_FORMAT_RGBA8,
    ZCSR_GL_L2D_FORMAT_RGBA16F,
    ZCSR_GL_L2D_FORMAT_RGBA8,
    ZCSR_GL_L2D_FORMAT_RGBA16F
};

typedef enum zcsr_light2d_phase {
    ZCSR_L2D_PHASE_IDLE = 0,
    ZCSR_L2D_PHASE_GEOMETRY,
    ZCSR_L2D_PHASE_LIGHTING,
    ZCSR_L2D_PHASE_DONE
} zcsr_light2d_phase;

/* Packed per-occluder attributes mirroring zcsr_light2d_occluder (fixed
 * capacity block in the caller buffer). */
typedef struct {
    float height;
    float factor;
    float distance;
    unsigned char flags; /* bit0 reverse, bit1 self_shadow, bit2 auto_disable */
} zcsr_l2d_occluder_attr;

#define ZCSR_L2D_OCC_REVERSE    0x01u
#define ZCSR_L2D_OCC_SELF       0x02u
#define ZCSR_L2D_OCC_AUTO_OFF   0x04u

struct zcsr_light2d_ctx {
    zcsr_gl_renderer* renderer;
    zcsr_light_quality quality;
    zcsr_light_quality max_quality;
    int width;
    int height;
    zcsr_gl_l2d_target gbuffer;
    zcsr_light* lights;
    float* uniform_staging; /* [0, capacity*4): LightData; next half: LightColor */
    size_t storage_capacity;
    size_t active_limit;
    size_t light_count;
    zcsr_light2d_phase phase;
    bool state_saved;
    zcsr_light2d_options options;
    const zcsr_camera* camera; /* caller's LIVE camera; NULL = built-in default */
    size_t occluder_count;
    uint8_t* occluder_verts; /* [ZCSR_LIGHT2D_MAX_OCCLUDERS]: vertex counts */
    float* occluder_xy;      /* flat [ZCSR_LIGHT2D_MAX_OCCLUDERS][..][2] */
    zcsr_l2d_occluder_attr* occluder_attrs;
};

/* Built-in camera for callers that never call set_camera: origin centered,
 * one 128px reference tile per world unit. Read-only shared storage. */
static const zcsr_camera zcsr_l2d_default_camera = {
    0.0f, 0.0f, 128.0f, 0, 0
};

static const zcsr_camera* zcsr_l2d_effective_camera(const zcsr_light2d_ctx* ctx)
{
    return (ctx->camera != NULL) ? ctx->camera : &zcsr_l2d_default_camera;
}

static bool zcsr_l2d_valid_quality(zcsr_light_quality q)
{
    return q >= ZCSR_LIGHT_QUALITY_LOW && q <= ZCSR_LIGHT_QUALITY_ULTRA;
}

static size_t zcsr_l2d_capacity_for_quality(zcsr_light_quality q)
{
    switch (q) {
    case ZCSR_LIGHT_QUALITY_LOW: return ZCSR_L2D_LOW_LIGHTS;
    case ZCSR_LIGHT_QUALITY_MEDIUM: return ZCSR_L2D_MEDIUM_LIGHTS;
    case ZCSR_LIGHT_QUALITY_HIGH: return ZCSR_L2D_MAX_LIGHTS;
    case ZCSR_LIGHT_QUALITY_ULTRA: return ZCSR_L2D_MAX_LIGHTS;
    default: return 0u;
    }
}

static bool zcsr_l2d_quality_implemented(zcsr_light_quality q)
{
    return q == ZCSR_LIGHT_QUALITY_LOW || q == ZCSR_LIGHT_QUALITY_HIGH;
}

static bool zcsr_l2d_add_size(size_t a, size_t b, size_t* out)
{
    if (out == NULL || a > SIZE_MAX - b) return false;
    *out = a + b;
    return true;
}

static bool zcsr_l2d_mul_size(size_t a, size_t b, size_t* out)
{
    if (out == NULL || (a != 0u && b > SIZE_MAX / a)) return false;
    *out = a * b;
    return true;
}

static bool zcsr_l2d_align_size(size_t value, size_t alignment, size_t* out)
{
    size_t remainder;
    size_t add;
    if (out == NULL || alignment == 0u) return false;
    remainder = value % alignment;
    if (remainder == 0u) {
        *out = value;
        return true;
    }
    add = alignment - remainder;
    return zcsr_l2d_add_size(value, add, out);
}

static bool zcsr_l2d_layout(size_t capacity,
                            size_t* lights_offset,
                            size_t* staging_offset,
                            size_t* occluders_offset,
                            size_t* occluder_attrs_offset,
                            size_t* inner_bytes)
{
    size_t offset;
    size_t bytes;
    size_t staging_floats;
    size_t staging_bytes;
    const size_t light_alignment = _Alignof(zcsr_light);
    const size_t float_alignment = _Alignof(float);
    const size_t attr_alignment = _Alignof(float);

    if (!zcsr_l2d_align_size(sizeof(struct zcsr_light2d_ctx),
                             light_alignment, &offset)) return false;
    if (lights_offset != NULL) *lights_offset = offset;
    if (!zcsr_l2d_mul_size(capacity, sizeof(zcsr_light), &bytes)) return false;
    if (!zcsr_l2d_add_size(offset, bytes, &offset)) return false;
    if (!zcsr_l2d_align_size(offset, float_alignment, &offset)) return false;
    if (staging_offset != NULL) *staging_offset = offset;
    if (!zcsr_l2d_mul_size(capacity, 8u, &staging_floats)) return false;
    if (!zcsr_l2d_mul_size(staging_floats, sizeof(float), &staging_bytes)) return false;
    if (!zcsr_l2d_add_size(offset, staging_bytes, &offset)) return false;
    /* occluder block: vertex counts + flat polygon coordinates + per-occluder
     * attributes (fixed capacity) */
    if (!zcsr_l2d_add_size(offset, (size_t)ZCSR_LIGHT2D_MAX_OCCLUDERS, &offset)) return false;
    if (occluders_offset != NULL) *occluders_offset = offset;
    if (!zcsr_l2d_mul_size((size_t)ZCSR_LIGHT2D_MAX_OCCLUDERS *
                               (size_t)ZCSR_LIGHT2D_MAX_OCCLUDER_VERTS * 2u,
                           sizeof(float), &bytes)) return false;
    if (!zcsr_l2d_add_size(offset, bytes, &offset)) return false;
    if (!zcsr_l2d_align_size(offset, attr_alignment, &offset)) return false;
    if (occluder_attrs_offset != NULL) *occluder_attrs_offset = offset;
    if (!zcsr_l2d_mul_size((size_t)ZCSR_LIGHT2D_MAX_OCCLUDERS,
                           sizeof(zcsr_l2d_occluder_attr), &bytes)) return false;
    if (!zcsr_l2d_add_size(offset, bytes, &offset)) return false;
    if (inner_bytes != NULL) *inner_bytes = offset;
    return true;
}

size_t zcsr_light2d_buffer_bytes_required(zcsr_light_quality max_quality)
{
    size_t inner_bytes;
    size_t capacity;
    size_t total;
    const size_t alignment_slack = ZCSR_L2D_MAX_ALIGN - 1u;

    if (!zcsr_l2d_valid_quality(max_quality)) return 0u;
    capacity = zcsr_l2d_capacity_for_quality(max_quality);
    if (capacity == 0u) return 0u;
    if (!zcsr_l2d_layout(capacity, NULL, NULL, NULL, NULL, &inner_bytes)) return 0u;
    if (!zcsr_l2d_add_size(inner_bytes, alignment_slack, &total)) return 0u;
    return total;
}

static bool zcsr_l2d_validate_light(const zcsr_light* light)
{
    if (light == NULL) return false;
    if (light->type != ZCSR_LIGHT_POINT && light->type != ZCSR_LIGHT_PARALLEL) return false;
    if (!isfinite(light->x) || !isfinite(light->y) || !isfinite(light->height) ||
        !isfinite(light->radius) || !isfinite(light->decay) ||
        !isfinite(light->dir_x) || !isfinite(light->dir_y) || !isfinite(light->dir_z) ||
        !isfinite(light->r) || !isfinite(light->g) ||
        !isfinite(light->b) || !isfinite(light->a)) return false;
    if (light->r < 0.0f || light->g < 0.0f || light->b < 0.0f || light->a < 0.0f) return false;
    if (light->radius <= 0.0f || light->decay < 0.0f) return false;
    if (light->type == ZCSR_LIGHT_PARALLEL) {
        const float d2 = light->dir_x * light->dir_x + light->dir_y * light->dir_y +
                         light->dir_z * light->dir_z;
        if (d2 < 1e-12f) return false; /* zero-length direction is meaningless */
    }
    return true;
}

static void zcsr_l2d_finish_failed_pass(zcsr_light2d_ctx* ctx)
{
    if (ctx == NULL) return;
    if (ctx->state_saved) {
        (void)zcsr_gl_l2d_state_pop(ctx->renderer);
        ctx->state_saved = false;
    }
    ctx->light_count = 0u;
    ctx->phase = ZCSR_L2D_PHASE_DONE;
}

zcsr_light2d_ctx* zcsr_light2d_create(zcsr_gl_renderer* renderer,
                                      zcsr_light_quality initial_quality,
                                      zcsr_light_quality max_quality,
                                      int width,
                                      int height,
                                      void* buffer,
                                      size_t bytes)
{
    size_t capacity;
    size_t required;
    size_t lights_offset;
    size_t staging_offset;
    size_t occluders_offset;
    size_t occluder_attrs_offset;
    size_t inner_bytes;
    size_t skip;
    size_t alignment;
    uintptr_t address;
    uint8_t* aligned_buffer;
    struct zcsr_light2d_ctx* ctx;

    if (renderer == NULL || buffer == NULL || width <= 0 || height <= 0) return NULL;
    if (!zcsr_l2d_valid_quality(initial_quality) || !zcsr_l2d_valid_quality(max_quality)) return NULL;
    if (initial_quality > max_quality) return NULL;
    if (!zcsr_l2d_quality_implemented(initial_quality)) return NULL;
    required = zcsr_light2d_buffer_bytes_required(max_quality);
    if (required == 0u || bytes < required) return NULL;

    capacity = zcsr_l2d_capacity_for_quality(max_quality);
    if (!zcsr_l2d_layout(capacity, &lights_offset, &staging_offset,
                         &occluders_offset, &occluder_attrs_offset,
                         &inner_bytes)) return NULL;

    alignment = ZCSR_L2D_MAX_ALIGN;
    address = (uintptr_t)buffer;
    skip = (size_t)((alignment - (address % alignment)) % alignment);
    if (skip > bytes || inner_bytes > bytes - skip) return NULL;
    aligned_buffer = (uint8_t*)buffer + skip;
    ctx = (struct zcsr_light2d_ctx*)aligned_buffer;
    memset(ctx, 0, sizeof(*ctx));

    ctx->renderer = renderer;
    ctx->quality = initial_quality;
    ctx->max_quality = max_quality;
    ctx->width = width;
    ctx->height = height;
    ctx->storage_capacity = capacity;
    ctx->active_limit = zcsr_l2d_capacity_for_quality(initial_quality);
    ctx->lights = (zcsr_light*)(aligned_buffer + lights_offset);
    ctx->uniform_staging = (float*)(void*)(aligned_buffer + staging_offset);
    ctx->gbuffer = ZCSR_GL_L2D_TARGET_INVALID;
    ctx->phase = ZCSR_L2D_PHASE_IDLE;
    ctx->options.shadows = true;
    ctx->options.normals = true;
    ctx->options.gain = 1.0f;
    ctx->options.light_scale_pct = 50;
    ctx->options.bloom = false;
    ctx->options.ray_shadows = false;
    ctx->camera = NULL; /* built-in default until set_camera */
    ctx->occluder_count = 0u;
    ctx->occluder_verts = aligned_buffer + occluders_offset;
    ctx->occluder_xy = (float*)(void*)(aligned_buffer + occluders_offset +
                                       (size_t)ZCSR_LIGHT2D_MAX_OCCLUDERS);
    ctx->occluder_attrs = (zcsr_l2d_occluder_attr*)(void*)
        (aligned_buffer + occluder_attrs_offset);

    if (max_quality >= ZCSR_LIGHT_QUALITY_HIGH) {
        if (!zcsr_gl_l2d_target_create(renderer, width, height,
                                       zcsr_l2d_gbuffer_formats, 4u,
                                       &ctx->gbuffer) ||
            ctx->gbuffer == ZCSR_GL_L2D_TARGET_INVALID) {
            if (ctx->gbuffer != ZCSR_GL_L2D_TARGET_INVALID) {
                zcsr_gl_l2d_target_destroy(renderer, ctx->gbuffer);
            }
            memset(ctx, 0, sizeof(*ctx));
            return NULL;
        }
    }
    return ctx;
}

void zcsr_light2d_destroy(zcsr_light2d_ctx* ctx)
{
    if (ctx == NULL) return;
    if (ctx->state_saved) {
        (void)zcsr_gl_l2d_state_pop(ctx->renderer);
        ctx->state_saved = false;
    }
    if (ctx->gbuffer != ZCSR_GL_L2D_TARGET_INVALID) {
        zcsr_gl_l2d_target_destroy(ctx->renderer, ctx->gbuffer);
        ctx->gbuffer = ZCSR_GL_L2D_TARGET_INVALID;
    }
    memset(ctx, 0, sizeof(*ctx));
}

bool zcsr_light2d_resize(zcsr_light2d_ctx* ctx, int width, int height)
{
    if (ctx == NULL || width <= 0 || height <= 0) return false;
    if (ctx->phase != ZCSR_L2D_PHASE_IDLE && ctx->phase != ZCSR_L2D_PHASE_DONE) return false;
    if (width == ctx->width && height == ctx->height) return true;
    if (ctx->gbuffer != ZCSR_GL_L2D_TARGET_INVALID) {
        if (!zcsr_gl_l2d_target_resize(ctx->renderer, ctx->gbuffer, width, height)) return false;
    }
    ctx->width = width;
    ctx->height = height;
    return true;
}

bool zcsr_light2d_set_quality(zcsr_light2d_ctx* ctx, zcsr_light_quality quality)
{
    if (ctx == NULL || !zcsr_l2d_valid_quality(quality)) return false;
    if (ctx->phase != ZCSR_L2D_PHASE_IDLE && ctx->phase != ZCSR_L2D_PHASE_DONE) return false;
    if (quality > ctx->max_quality || !zcsr_l2d_quality_implemented(quality)) return false;
    if (quality == ZCSR_LIGHT_QUALITY_HIGH && ctx->gbuffer == ZCSR_GL_L2D_TARGET_INVALID) return false;
    ctx->quality = quality;
    ctx->active_limit = zcsr_l2d_capacity_for_quality(quality);
    ctx->light_count = 0u;
    return true;
}

bool zcsr_light2d_begin_geometry(zcsr_light2d_ctx* ctx)
{
    if (ctx == NULL) return false;
    if (ctx->phase != ZCSR_L2D_PHASE_IDLE && ctx->phase != ZCSR_L2D_PHASE_DONE) return false;
    ctx->light_count = 0u;
    ctx->phase = ZCSR_L2D_PHASE_GEOMETRY;

    if (ctx->quality == ZCSR_LIGHT_QUALITY_HIGH) {
        if (ctx->gbuffer == ZCSR_GL_L2D_TARGET_INVALID ||
            !zcsr_gl_l2d_state_push(ctx->renderer)) {
            ctx->phase = ZCSR_L2D_PHASE_DONE;
            return false;
        }
        ctx->state_saved = true;
        if (!zcsr_gl_l2d_gbuffer_begin(ctx->renderer, ctx->gbuffer,
                                       ctx->width, ctx->height)) {
            zcsr_l2d_finish_failed_pass(ctx);
            return false;
        }
    }
    return true;
}

bool zcsr_light2d_begin_lighting(zcsr_light2d_ctx* ctx)
{
    if (ctx == NULL || ctx->phase != ZCSR_L2D_PHASE_GEOMETRY) return false;

    if (ctx->quality == ZCSR_LIGHT_QUALITY_LOW) {
        if (!zcsr_gl_l2d_state_push(ctx->renderer)) {
            ctx->phase = ZCSR_L2D_PHASE_DONE;
            return false;
        }
        ctx->state_saved = true;
    } else if (ctx->quality == ZCSR_LIGHT_QUALITY_HIGH) {
        if (!zcsr_gl_l2d_bind_default_target(ctx->renderer, ctx->width, ctx->height) ||
            !zcsr_gl_l2d_bind_target_textures(ctx->renderer, ctx->gbuffer,
                                              ZCSR_L2D_FIRST_TEXTURE_UNIT)) {
            zcsr_l2d_finish_failed_pass(ctx);
            return false;
        }
    } else {
        ctx->phase = ZCSR_L2D_PHASE_DONE;
        return false;
    }

    ctx->phase = ZCSR_L2D_PHASE_LIGHTING;
    return true;
}

bool zcsr_light2d_add_light(zcsr_light2d_ctx* ctx, const zcsr_light* light)
{
    if (ctx == NULL || ctx->phase != ZCSR_L2D_PHASE_LIGHTING) return false;
    if (!zcsr_l2d_validate_light(light)) return false;
    if (ctx->light_count >= ctx->active_limit ||
        ctx->light_count >= ctx->storage_capacity) return false;
    ctx->lights[ctx->light_count] = *light;
    ++ctx->light_count;
    return true;
}

bool zcsr_light2d_clear_lights(zcsr_light2d_ctx* ctx)
{
    if (ctx == NULL) return false;
    if (ctx->phase != ZCSR_L2D_PHASE_IDLE && ctx->phase != ZCSR_L2D_PHASE_DONE) return false;
    ctx->light_count = 0u;
    return true;
}

bool zcsr_light2d_set_options(zcsr_light2d_ctx* ctx, const zcsr_light2d_options* options)
{
    if (ctx == NULL || options == NULL) return false;
    if (ctx->phase != ZCSR_L2D_PHASE_IDLE && ctx->phase != ZCSR_L2D_PHASE_DONE) return false;
    if (!isfinite(options->gain) || options->gain < 0.0f) return false;
    if (options->light_scale_pct != 25 && options->light_scale_pct != 50 &&
        options->light_scale_pct != 100) return false;
    ctx->options = *options;
    return true;
}

void zcsr_light2d_get_options(const zcsr_light2d_ctx* ctx, zcsr_light2d_options* out)
{
    if (out == NULL) return;
    if (ctx == NULL) {
        zcsr_light2d_options defaults;
        defaults.shadows = true;
        defaults.normals = true;
        defaults.gain = 1.0f;
        defaults.light_scale_pct = 50;
        defaults.bloom = false;
        defaults.ray_shadows = false;
        *out = defaults;
        return;
    }
    *out = ctx->options;
}

bool zcsr_light2d_set_camera(zcsr_light2d_ctx* ctx, const zcsr_camera* camera)
{
    if (ctx == NULL || camera == NULL) return false;
    if (ctx->phase != ZCSR_L2D_PHASE_IDLE && ctx->phase != ZCSR_L2D_PHASE_DONE) return false;
    if (!isfinite(camera->center_x) || !isfinite(camera->center_y)) return false;
    if (!isfinite(camera->px_per_unit) || camera->px_per_unit <= 0.0f) return false;
    /* stores the LIVE pointer: pan/zoom edits apply next frame, no re-set */
    ctx->camera = camera;
    return true;
}

void zcsr_light2d_get_camera(const zcsr_light2d_ctx* ctx, zcsr_camera* out)
{
    if (out == NULL) return;
    if (ctx == NULL) {
        *out = zcsr_l2d_default_camera;
        return;
    }
    *out = *zcsr_l2d_effective_camera(ctx);
}

zcsr_gl_l2d_target zcsr_light2d_gbuffer_handle(const zcsr_light2d_ctx* ctx)
{
    if (ctx == NULL) return ZCSR_GL_L2D_TARGET_INVALID;
    return ctx->gbuffer;
}

bool zcsr_light2d_add_occluder(zcsr_light2d_ctx* ctx, const zcsr_light2d_occluder* occluder)
{
    size_t i;
    float* dst;
    zcsr_l2d_occluder_attr* attr;
    if (ctx == NULL || occluder == NULL || occluder->xy == NULL) return false;
    if (ctx->phase != ZCSR_L2D_PHASE_IDLE && ctx->phase != ZCSR_L2D_PHASE_DONE) return false;
    if (occluder->count < 3u || occluder->count > (size_t)ZCSR_LIGHT2D_MAX_OCCLUDER_VERTS) return false;
    if (ctx->occluder_count >= (size_t)ZCSR_LIGHT2D_MAX_OCCLUDERS) return false;
    for (i = 0u; i < occluder->count * 2u; ++i) {
        if (!isfinite(occluder->xy[i])) return false;
    }
    if (!isfinite(occluder->height) || !isfinite(occluder->factor) ||
        !isfinite(occluder->distance)) return false;
    if (occluder->factor <= 0.0f || occluder->factor > 1.0f) return false;
    dst = ctx->occluder_xy +
        ctx->occluder_count * (size_t)ZCSR_LIGHT2D_MAX_OCCLUDER_VERTS * 2u;
    for (i = 0u; i < occluder->count * 2u; ++i) dst[i] = occluder->xy[i];
    ctx->occluder_verts[ctx->occluder_count] = (uint8_t)occluder->count;
    attr = &ctx->occluder_attrs[ctx->occluder_count];
    attr->height = occluder->height;
    attr->factor = occluder->factor;
    attr->distance = occluder->distance;
    attr->flags = (unsigned char)
        ((occluder->reverse ? ZCSR_L2D_OCC_REVERSE : 0u) |
         (occluder->self_shadow ? ZCSR_L2D_OCC_SELF : 0u) |
         (occluder->auto_disable ? ZCSR_L2D_OCC_AUTO_OFF : 0u));
    ++ctx->occluder_count;
    return true;
}

void zcsr_light2d_clear_occluders(zcsr_light2d_ctx* ctx)
{
    if (ctx == NULL) return;
    ctx->occluder_count = 0u;
}

static bool zcsr_l2d_draw_low(zcsr_light2d_ctx* ctx)
{
    size_t i;
    bool ok = true;

    if (!zcsr_gl_l2d_set_program(ctx->renderer, ZCSR_GL_L2D_PROGRAM_RADIAL) ||
        !zcsr_gl_l2d_set_blend_mode(ctx->renderer, ZCSR_GL_L2D_BLEND_ADDITIVE)) {
        return false;
    }
    for (i = 0u; i < ctx->light_count; ++i) {
        const zcsr_light* light = &ctx->lights[i];
        float qx = light->x - light->radius;
        float qy = light->y - light->radius;
        float qsize = light->radius * 2.0f;
        bool light_ok = true;
        /* parallel lights have no radial quad — HIGH quality only */
        if (light->type != ZCSR_LIGHT_POINT) continue;
        /* Derived quad coords can overflow to infinity even from finite inputs; reject the
         * pass explicitly instead of feeding GL non-finite geometry. */
        if (!isfinite(qx) || !isfinite(qy) || !isfinite(qsize)) return false;
        light_ok = zcsr_gl_l2d_uniform_1f(ctx->renderer, "u_Radius", light->radius) && light_ok;
        light_ok = zcsr_gl_l2d_uniform_1f(ctx->renderer, "u_Decay", light->decay) && light_ok;
        light_ok = zcsr_gl_l2d_uniform_4f(ctx->renderer, "u_LightColor",
                                          light->r, light->g, light->b, light->a) && light_ok;
        if (!light_ok || !zcsr_gl_l2d_draw_quad(ctx->renderer, qx, qy, qsize, qsize)) {
            ok = false;
        }
    }
    return ok;
}

/* Renders every occluder into the current shadow mask with the reference
 * engine's Block/Wall/Self semantics: per-occluder height gating, finite
 * extrusion bands, reverse extrusion, self-shadow fills. Point lights select
 * back-facing edges by the winding-independent centroid test and extrude
 * radially; parallel lights select edges facing away from the travel
 * direction and extrude along it. Everything is projected to pixels through
 * the camera first. */
static void zcsr_l2d_emit_occluders(zcsr_light2d_ctx* ctx,
                                    const zcsr_light* light,
                                    float lx_px, float ly_px,
                                    float dir_x, float dir_y)
{
    const zcsr_camera* cam = zcsr_l2d_effective_camera(ctx);
    const float ppu = cam->px_per_unit;
    const float half_w = (float)ctx->width * 0.5f;
    const float half_h = (float)ctx->height * 0.5f;
    const int parallel = (light->type == ZCSR_LIGHT_PARALLEL) ? 1 : 0;
    size_t o, j;
    for (o = 0u; o < ctx->occluder_count; ++o) {
        const float* poly = ctx->occluder_xy +
            o * (size_t)ZCSR_LIGHT2D_MAX_OCCLUDER_VERTS * 2u;
        const zcsr_l2d_occluder_attr* attr = &ctx->occluder_attrs[o];
        const unsigned int n = ctx->occluder_verts[o];
        const int reverse = (attr->flags & ZCSR_L2D_OCC_REVERSE) ? 1 : 0;
        float cx = 0.0f;
        float cy = 0.0f;
        /* distance: world units -> px; <= 0 means the infinite wedge */
        const float dist_px =
            (attr->distance > 0.0f) ? (attr->distance * ppu) : 100000.0f;

        /* auto_disable: an occluder at/above the light's height is lit and
         * casts nothing (reference light_is_above_block). */
        if ((attr->flags & ZCSR_L2D_OCC_AUTO_OFF) && attr->height >= light->height) {
            continue;
        }

        for (j = 0u; j < (size_t)n; ++j) {
            cx += poly[j * 2u];
            cy += poly[j * 2u + 1u];
        }
        cx /= (float)n;
        cy /= (float)n;

        if (attr->flags & ZCSR_L2D_OCC_SELF) {
            /* SelfShadow: cover the polygon itself while the light is ABOVE
             * the occluder (or always when auto_disable is off). */
            const int covered = (attr->flags & ZCSR_L2D_OCC_AUTO_OFF)
                ? (light->height > attr->height) : 1;
            if (covered) {
                float pxs[ZCSR_LIGHT2D_MAX_OCCLUDER_VERTS * 2u];
                for (j = 0u; j < (size_t)n; ++j) {
                    pxs[j * 2u] = (poly[j * 2u] - cam->center_x) * ppu + half_w;
                    pxs[j * 2u + 1u] = (poly[j * 2u + 1u] - cam->center_y) * ppu + half_h;
                }
                (void)zcsr_gl_l2d_shadow_fill(ctx->renderer, pxs, (size_t)n, attr->factor);
            }
            continue;
        }

        for (j = 0u; j < (size_t)n; ++j) {
            const size_t k = (j + 1u) % (size_t)n;
            const float ax = (poly[j * 2u] - cam->center_x) * ppu + half_w;
            const float ay = (poly[j * 2u + 1u] - cam->center_y) * ppu + half_h;
            const float bx = (poly[k * 2u] - cam->center_x) * ppu + half_w;
            const float by = (poly[k * 2u + 1u] - cam->center_y) * ppu + half_h;
            const float nx = -(by - ay);
            const float ny = bx - ax;
            int emit;
            if (parallel) {
                /* edge normal vs the travel direction: facing away casts */
                const float d = nx * dir_x + ny * dir_y;
                emit = reverse ? (d < 0.0f) : (d > 0.0f);
            } else {
                const float sc = nx * ((cx - cam->center_x) * ppu + half_w - ax) +
                                 ny * ((cy - cam->center_y) * ppu + half_h - ay);
                const float sl = nx * (lx_px - ax) + ny * (ly_px - ay);
                emit = reverse ? (sc * sl > 0.0f) : (sc * sl < 0.0f);
            }
            if (emit) {
                (void)zcsr_gl_l2d_shadow_edge(ctx->renderer, ax, ay, bx, by,
                                              attr->factor, reverse, dist_px);
            }
        }
    }
}

static bool zcsr_l2d_draw_high(zcsr_light2d_ctx* ctx)
{
    /* World-unit falloff: 1.0 = one reference tile (the camera's px_per_unit
     * default of 128 px matches one 128px tile), like the reference engine's
     * d/(d+1) in tile units. */
    const zcsr_camera* cam = zcsr_l2d_effective_camera(ctx);
    const float dist_scale = 1.0f;
    const float ppu = cam->px_per_unit;
    const float half_w = (float)ctx->width * 0.5f;
    const float half_h = (float)ctx->height * 0.5f;
    int scale = ctx->options.light_scale_pct;
    int buf_w = (ctx->width * scale) / 100;
    int buf_h = (ctx->height * scale) / 100;
    size_t i;
    bool ok = true;

    if (buf_w <= 0) buf_w = 1;
    if (buf_h <= 0) buf_h = 1;

    /* Light accumulation: one additive pass per light at the reduced buffer scale.
     * Shadow-casting lights render their mask first; shadow_end leaves it bound on
     * unit 8 for the immediately following light_emit. */
    if (!zcsr_gl_l2d_lightbuf_begin(ctx->renderer, buf_w, buf_h,
                                    ctx->width, ctx->height,
                                    ctx->options.gain, dist_scale, ppu,
                                    ctx->options.normals ? 1 : 0)) {
        return false;
    }

    /* ray_shadows: upload every occluder as footprint AABB + height (2 vec4
     * each, packed into the staging buffer) and let the light shader run the
     * per-pixel segment-vs-prism test; the 2D extrusion mask is bypassed. */
    if (ctx->options.ray_shadows && ctx->occluder_count > 0u) {
        size_t box_count = 0u;
        size_t v;
        for (i = 0u; i < ctx->occluder_count && box_count < 8u; ++i) {
            const float* poly = ctx->occluder_xy +
                i * (size_t)ZCSR_LIGHT2D_MAX_OCCLUDER_VERTS * 2u;
            const unsigned int n = ctx->occluder_verts[i];
            float min_x = poly[0];
            float min_y = poly[1];
            float max_x = poly[0];
            float max_y = poly[1];
            for (v = 1u; v < (size_t)n; ++v) {
                if (poly[v * 2u] < min_x) min_x = poly[v * 2u];
                if (poly[v * 2u] > max_x) max_x = poly[v * 2u];
                if (poly[v * 2u + 1u] < min_y) min_y = poly[v * 2u + 1u];
                if (poly[v * 2u + 1u] > max_y) max_y = poly[v * 2u + 1u];
            }
            ctx->uniform_staging[box_count * 8u + 0u] = min_x;
            ctx->uniform_staging[box_count * 8u + 1u] = min_y;
            ctx->uniform_staging[box_count * 8u + 2u] = max_x;
            ctx->uniform_staging[box_count * 8u + 3u] = max_y;
            ctx->uniform_staging[box_count * 8u + 4u] = ctx->occluder_attrs[i].height;
            ctx->uniform_staging[box_count * 8u + 5u] = 0.0f;
            ctx->uniform_staging[box_count * 8u + 6u] = 0.0f;
            ctx->uniform_staging[box_count * 8u + 7u] = 0.0f;
            ++box_count;
        }
        if (!zcsr_gl_l2d_uniform_vec4_array(ctx->renderer, "u_Boxes",
                                            ctx->uniform_staging,
                                            box_count * 2u) ||
            !zcsr_gl_l2d_uniform_1i(ctx->renderer, "u_BoxCount", (int)box_count)) {
            return false;
        }
    }
    if (!zcsr_gl_l2d_uniform_1i(ctx->renderer, "u_ray_shadows",
                                (ctx->options.ray_shadows &&
                                 ctx->occluder_count > 0u) ? 1 : 0)) {
        return false;
    }

    for (i = 0u; i < ctx->light_count; ++i) {
        const zcsr_light* light = &ctx->lights[i];
        const int parallel = (light->type == ZCSR_LIGHT_PARALLEL) ? 1 : 0;
        const float px = (light->x - cam->center_x) * ppu + half_w;
        const float py = (light->y - cam->center_y) * ppu + half_h;
        float dir_x = 0.0f;
        float dir_y = 0.0f;
        float dir3_x = 0.0f;
        float dir3_y = 0.0f;
        float dir3_z = 1.0f;
        zcsr_gl_l2d_light_draw draw;
        int use_shadow = (ctx->options.shadows && ctx->options.ray_shadows &&
                          ctx->occluder_count > 0u && !parallel) ? 1 : 0;

        if (parallel) {
            const float len3 = sqrtf(light->dir_x * light->dir_x +
                                     light->dir_y * light->dir_y +
                                     light->dir_z * light->dir_z);
            dir3_x = light->dir_x / len3;
            dir3_y = light->dir_y / len3;
            dir3_z = light->dir_z / len3;
            /* shadow extrusion follows the in-plane projection */
            dir_x = dir3_x;
            dir_y = dir3_y;
            {
                const float len2 = sqrtf(dir_x * dir_x + dir_y * dir_y);
                if (len2 > 1e-6f) {
                    dir_x /= len2;
                    dir_y /= len2;
                } else {
                    dir_x = 0.0f;
                    dir_y = 0.0f;
                }
            }
        }

        if (ctx->options.shadows && ctx->occluder_count > 0u &&
            !(ctx->options.ray_shadows && !parallel)) {
            use_shadow = 1;
            if (!zcsr_gl_l2d_shadow_begin(ctx->renderer, buf_w, buf_h,
                                          ctx->width, ctx->height,
                                          px, py, dir_x, dir_y,
                                          100000.0f, parallel)) {
                return false;
            }
            zcsr_l2d_emit_occluders(ctx, light, px, py, dir_x, dir_y);
            if (!zcsr_gl_l2d_shadow_end(ctx->renderer)) {
                return false;
            }
        }

        draw.quad_x = px;
        draw.quad_y = py;
        draw.wx = light->x;
        draw.wy = light->y;
        draw.height = light->height;
        draw.radius = light->radius;
        draw.decay = light->decay;
        draw.dir_x = dir3_x;
        draw.dir_y = dir3_y;
        draw.dir_z = dir3_z;
        draw.parallel = parallel;
        /* per-light intensity: color rgb x a, like the reference engine's
         * light_color.w x gain */
        draw.cr = light->r * light->a;
        draw.cg = light->g * light->a;
        draw.cb = light->b * light->a;
        draw.use_shadow = use_shadow;
        ok = zcsr_gl_l2d_light_emit(ctx->renderer, &draw);
        if (!ok) return false;
    }

    if (!zcsr_gl_l2d_lightbuf_end(ctx->renderer)) return false;

    /* Composite to the DEFAULT WINDOW target (lightbuf_end leaves the light
     * FBO bound — without this rebinding the composite renders into the
     * accumulation buffer and the window stays cleared): linear-space
     * lighting, tonemap, gamma — bloom variant when enabled. */
    if (!zcsr_gl_l2d_bind_default_target(ctx->renderer, ctx->width, ctx->height)) {
        return false;
    }
    ok = zcsr_gl_l2d_set_program(ctx->renderer, ctx->options.bloom
                                 ? ZCSR_GL_L2D_PROGRAM_DEFERRED_BLOOM
                                 : ZCSR_GL_L2D_PROGRAM_DEFERRED);
    if (ok && ctx->options.bloom) {
        ok = zcsr_gl_l2d_uniform_2f(ctx->renderer, "u_LightRes",
                                    (float)buf_w, (float)buf_h);
    }
    ok = zcsr_gl_l2d_set_blend_mode(ctx->renderer, ZCSR_GL_L2D_BLEND_OPAQUE) && ok;
    ok = zcsr_gl_l2d_uniform_1i(ctx->renderer, "u_Albedo", ZCSR_L2D_FIRST_TEXTURE_UNIT + 0) && ok;
    ok = zcsr_gl_l2d_uniform_1i(ctx->renderer, "u_Emissive", ZCSR_L2D_FIRST_TEXTURE_UNIT + 3) && ok;
    ok = zcsr_gl_l2d_uniform_1i(ctx->renderer, "u_Light", ZCSR_L2D_FIRST_TEXTURE_UNIT + 4) && ok;
    ok = zcsr_gl_l2d_uniform_3f(ctx->renderer, "u_Ambient", 0.03f, 0.03f, 0.03f) && ok;
    ok = zcsr_gl_l2d_bind_light_texture(ctx->renderer, ZCSR_L2D_FIRST_TEXTURE_UNIT + 4) && ok;
    if (ok) ok = zcsr_gl_l2d_draw_fullscreen_quad(ctx->renderer);
    return ok;
}

bool zcsr_light2d_end_lighting(zcsr_light2d_ctx* ctx)
{
    bool ok;
    bool pop_ok = true;
    if (ctx == NULL || ctx->phase != ZCSR_L2D_PHASE_LIGHTING) return false;

    if (ctx->quality == ZCSR_LIGHT_QUALITY_LOW) {
        ok = zcsr_l2d_draw_low(ctx);
    } else if (ctx->quality == ZCSR_LIGHT_QUALITY_HIGH) {
        ok = zcsr_l2d_draw_high(ctx);
    } else {
        ok = false;
    }

    if (ctx->state_saved) {
        pop_ok = zcsr_gl_l2d_state_pop(ctx->renderer);
        ctx->state_saved = false;
    }
    ctx->light_count = 0u;
    ctx->phase = ZCSR_L2D_PHASE_DONE;
    return ok && pop_ok;
}
