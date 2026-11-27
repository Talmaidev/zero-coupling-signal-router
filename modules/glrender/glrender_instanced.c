/* glrender_instanced.c — instanced rendering program, init/destroy, draw, sort stage, state sort.
 * Uses core's texture table (zcsr_gl_find_tex) and platform hooks (make_current/swap/update_size).
 *
 * Tier axes (render_tier.h) are selected at compile/assembly time; the hot
 * path carries no per-frame bool argument (ruling 5).
 */
#include "glrender_internal.h"
#include "zcsr/diagnostics.h"
#include "zcsr/color_shader_segments.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* glad exposes GL_TEXTURE0 but not the consecutive unit-1 token on every
 * platform; keep the same local fallback used by glrender_l2d_geo.c. */
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif

#if ZCSR_INST_PERSISTENT_RING
/* Optional ARB_buffer_storage entries, resolved once through the platform
 * loader (ruling 9). The vendored loader does not carry these symbols. */
typedef void*  (*zcsr_pfn_map_buffer_range)(unsigned int, long long, size_t,
                                             unsigned int);
typedef void   (*zcsr_pfn_buffer_storage)(unsigned int, size_t, const void*, unsigned int);
typedef int    (*zcsr_pfn_unmap_buffer)(unsigned int);
static zcsr_pfn_map_buffer_range z_persist_map_range = NULL;
static zcsr_pfn_buffer_storage  z_persist_buffer_storage = NULL;
static zcsr_pfn_unmap_buffer    z_persist_unmap = NULL;
#endif

/* Plain record contract: 10 floats = 40 bytes, no tint/mode/amount slot. */
ZCSR_TIER_ASSERT(sizeof(zcsr_gl_instance_plain) == 40u, plain_stride);

/* Ruling 6: per-instance upload stride for the active layout tier. The public
 * API still receives the full 168 B descriptor (culling / sort / classification
 * read it); the VBO and VAO expose only the bytes this tier compiles. */
static inline size_t zcsr_tier_stride_bytes(void) {
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_POS_UV
    return ZCSR_INSTANCE_STRIDE_POS_UV;
#elif ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
    return ZCSR_INSTANCE_STRIDE_COMPACT;
#else
    return ZCSR_INSTANCE_STRIDE_FULL;
#endif
}

#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
/* glVertexAttribIPointer is not carried by the vendored glad loader; resolve
 * it through the same optional-entry platform hook used for the persistent ring
 * (core GL 3.0, present in the 3.3 context this renderer targets). COMPACT
 * tier only: the FULL / POS_UV records expose plain float slots. */
typedef void (*zcsr_pfn_vertex_attrib_i_pointer)(GLuint, GLint, GLenum, GLsizei,
                                                 const void*);
static zcsr_pfn_vertex_attrib_i_pointer z_attrib_i_pointer = NULL;
#endif

#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
/* Bind one instanced attribute slot. UINT slots (the COMPACT packed record)
 * go through the integer attribute pointer so the shader reads `in uint`;
 * every other slot stays the legacy float pointer. COMPACT tier only. */
static void zcsr_gl_bind_inst_attr(const zcsr_tier_attr* attr, GLsizei stride) {
    glEnableVertexAttribArray(attr->location);
    if (attr->type == ZCSR_ATTR_UINT) {
        if (z_attrib_i_pointer) {
            z_attrib_i_pointer(attr->location, attr->count, GL_UNSIGNED_INT,
                               stride, (void*)attr->offset);
        }
    } else {
        glVertexAttribPointer(attr->location, attr->count, GL_FLOAT, GL_FALSE,
                              stride, (void*)attr->offset);
    }
    glVertexAttribDivisor(attr->location, 1);
}
#endif

/* ===========================================================================================
 * Instanced shader / program
 * =========================================================================================== */

/* Tier-gated fragment source (ruling 8). LINEAR_HDR assembles the HSV / OKLab /
 * recolor segments owned by the color pipeline module; DIRECT_SRGB compiles
 * none of those stages and writes the modulation result straight out. The tier
 * is a compile-time macro — there is no per-frame stage switch. */
static const char* zcsr_gl_inst_fragment_source(void) {
    static char fs[9216];
    size_t n = 0;
#define ZFS_APPEND(...) do { n += snprintf(fs + n, sizeof(fs) - n, __VA_ARGS__); } while (0)
    ZFS_APPEND("%s", ZCSR_GL_GLSL_VERSION);
    ZFS_APPEND("%s", ZCSR_GL_GLSL_FRAGMENT_PRECISION);
    ZFS_APPEND(
"in vec2 v_uv;\n"
"in vec4 v_color;\n"
"in vec2 v_mod;\n"
"in vec4 v_recolor;\n"
"in vec4 v_ramp;\n"
"in vec4 v_glow;\n"
"in vec4 v_chroma;\n"
"in vec4 v_emiss;\n"
"in vec4 v_rim;\n"
"uniform sampler2D u_tex;\n"
"uniform sampler2D u_ramp;\n"
"out vec4 o_color;\n");
    ZFS_APPEND("%s", zcsr_glsl_display_to_linear);
#if ZCSR_COLOR_TIER == ZCSR_COLOR_OPT_LINEAR_HDR
    ZFS_APPEND("%s", zcsr_glsl_hsv_pair);
    ZFS_APPEND("%s", zcsr_glsl_oklab_pair);
    ZFS_APPEND("%s", zcsr_glsl_apply_recolor);
    ZFS_APPEND(
"void main(){\n"
"  vec4 texel = texture(u_tex, v_uv);\n"
"  vec4 tint = vec4(display_to_linear(v_color.rgb), v_color.a);\n"
"  float amount = clamp(v_mod.y, 0.0, 1.0);\n"
"  if (v_mod.x < 0.5) o_color = texel * tint;\n"
"  else if (v_mod.x < 1.5) o_color = mix(texel, tint, amount);\n"
"  else o_color = clamp(texel + tint * amount, 0.0, 1.0);\n"
"  o_color.rgb = apply_recolor(o_color.rgb, v_recolor, v_ramp, v_chroma);\n"
"  if (v_glow.w > 0.0) {\n"
"    vec3 glow = display_to_linear(clamp(v_glow.rgb, 0.0, 1.0));\n"
"    o_color.rgb += glow * (clamp(v_glow.w, 0.0, 1.0) * clamp(o_color.a, 0.0, 1.0));\n"
"  }\n"
"  if (v_emiss.w > 0.0) {\n"
"    /* HDR core: only the bright source pixels emit, and the unclamped\n"
"     * add is resolved once by the frame tonemap. */\n"
"    float src_luma = dot(clamp(texel.rgb, 0.0, 1.0), vec3(0.2126, 0.7152, 0.0722));\n"
"    float core = smoothstep(0.45, 0.95, src_luma);\n"
"    core = core * core;\n"
"    o_color.rgb += display_to_linear(clamp(v_emiss.rgb, 0.0, 1.0)) *\n"
"                   (v_emiss.w * core * clamp(o_color.a, 0.0, 1.0));\n"
"  }\n"
"  if (v_rim.w > 0.0) {\n"
"    /* Alpha silhouette band: this is a separate frost/energy shell, not\n"
"     * a whole-sprite tint. */\n"
"    vec2 px = 1.0 / max(vec2(textureSize(u_tex, 0)), vec2(1.0));\n"
"    float n = max(max(texture(u_tex, v_uv + vec2(px.x, 0.0)).a,\n"
"                      texture(u_tex, v_uv - vec2(px.x, 0.0)).a),\n"
"                  max(texture(u_tex, v_uv + vec2(0.0, px.y)).a,\n"
"                      texture(u_tex, v_uv - vec2(0.0, px.y)).a));\n"
"    float edge = clamp(o_color.a - n, 0.0, 1.0);\n"
"    o_color.rgb += display_to_linear(clamp(v_rim.rgb, 0.0, 1.0)) *\n"
"                   (v_rim.w * edge);\n"
"  }\n"
"}\n");
#else
    /* DIRECT_SRGB: no linear-frame encode pass, no OKLab / recolor / ramp /
     * chroma. The full-program varyings are still declared by the vertex stage;
     * this slim stage never reads them. */
    ZFS_APPEND(
"void main(){\n"
"  vec4 texel = texture(u_tex, v_uv);\n"
"  vec4 tint = vec4(clamp(v_color.rgb, 0.0, 1.0), v_color.a);\n"
"  float amount = clamp(v_mod.y, 0.0, 1.0);\n"
"  if (v_mod.x < 0.5) o_color = texel * tint;\n"
"  else if (v_mod.x < 1.5) o_color = mix(texel, tint, amount);\n"
"  else o_color = clamp(texel + tint * amount, 0.0, 1.0);\n"
"}\n");
#endif
#undef ZFS_APPEND
    return fs;
}

static unsigned int zcsr_gl_make_inst_program(void) {
    /* Unit-quad corner (per-vertex) + per-instance attributes (divisor 1). The vertex shader
     * expands the corner into the rotated quad, so the CPU only uploads instance data. */
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
    /* COMPACT 36 B: center/size/UV + one packed uint32 (rotation normalized
     * short, 8-bit alpha, reserved). Tint/recolor/ramp/glow/chroma/emiss/rim
     * slots are not compiled; the matching varyings are zeroed so the shared
     * fragment contract still links. */
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_corner;\n"
        "layout(location=1) in vec2 i_center;\n"
        "layout(location=2) in vec2 i_size;\n"
        "layout(location=3) in vec4 i_uv;\n"
        "layout(location=4) in uint i_pack;\n"
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_color;\n"
        "out vec2 v_mod;\n"
        "out vec4 v_recolor;\n"
        "out vec4 v_ramp;\n"
        "out vec4 v_glow;\n"
        "out vec4 v_chroma;\n"
        "out vec4 v_emiss;\n"
        "out vec4 v_rim;\n"
        "void main(){\n"
        "  vec2 halfsize = i_size * 0.5;\n"
        "  float rot = float(i_pack & 65535u) * 6.28318530718 / 65536.0;\n"
        "  float c = cos(rot);\n"
        "  float s = sin(rot);\n"
        "  vec2 local = a_corner * halfsize;\n"
        "  vec2 pos = i_center + vec2(local.x*c - local.y*s, local.x*s + local.y*c);\n"
        "  gl_Position = vec4((pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (pos.y / u_view.y) * 2.0, 0.0, 1.0);\n"
        "  vec2 t = a_corner * 0.5 + 0.5;\n"
        "  v_uv = mix(i_uv.xy, i_uv.zw, t);\n"
        "  float alpha = float((i_pack >> 16) & 255u) / 255.0;\n"
        "  v_color = vec4(1.0, 1.0, 1.0, alpha);\n"
        "  v_mod = vec2(0.0, 0.0);\n"
        "  v_recolor = vec4(0.0); v_ramp = vec4(0.0); v_glow = vec4(0.0);\n"
        "  v_chroma = vec4(0.0); v_emiss = vec4(0.0); v_rim = vec4(0.0);\n"
        "}\n";
#else
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_corner;\n"
        "layout(location=1) in vec2 i_center;\n"
        "layout(location=2) in vec2 i_size;\n"
        "layout(location=3) in vec4 i_uv;\n"
        "layout(location=4) in vec4 i_color;\n"
        "layout(location=5) in float i_rotation;\n"
        "layout(location=6) in float i_amount;\n"
        "layout(location=7) in float i_mode;\n"
        "layout(location=8) in vec4 i_recolor;\n"
        "layout(location=9) in vec4 i_ramp;\n"
        "layout(location=10) in vec4 i_glow;\n"
        "layout(location=11) in vec4 i_chroma;\n"
        "layout(location=12) in vec4 i_emiss;\n"
        "layout(location=13) in vec4 i_rim;\n"
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_color;\n"
        "out vec2 v_mod;\n"
        "out vec4 v_recolor;\n"
        "out vec4 v_ramp;\n"
        "out vec4 v_glow;\n"
        "out vec4 v_chroma;\n"
        "out vec4 v_emiss;\n"
        "out vec4 v_rim;\n"
        "void main(){\n"
        "  vec2 halfsize = i_size * 0.5;\n"
        "  float c = cos(i_rotation);\n"
        "  float s = sin(i_rotation);\n"
        "  vec2 local = a_corner * halfsize;\n"
        "  vec2 pos = i_center + vec2(local.x*c - local.y*s, local.x*s + local.y*c);\n"
        "  gl_Position = vec4((pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (pos.y / u_view.y) * 2.0, 0.0, 1.0);\n"
        "  vec2 t = a_corner * 0.5 + 0.5;\n"
        "  v_uv = mix(i_uv.xy, i_uv.zw, t);\n"
        "  v_color = i_color; v_mod = vec2(i_mode, i_amount);\n"
        "  v_recolor = i_recolor; v_ramp = i_ramp; v_glow = i_glow; v_chroma = i_chroma;\n"
        "  v_emiss = i_emiss; v_rim = i_rim;\n"
        "}\n";
#endif
    const char* fs = zcsr_gl_inst_fragment_source();
    unsigned int v = zcsr_gl_compile(GL_VERTEX_SHADER, vs);
    unsigned int f = zcsr_gl_compile(GL_FRAGMENT_SHADER, fs);
    unsigned int p;
    int ok = 0;
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    p = glCreateProgram();
    if (!p) { glDeleteShader(v); glDeleteShader(f); return 0; }
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        zcsr_gl_log_object_info("instanced program link failed", p, true);
        glDeleteProgram(p); return 0;
    }
    return p;
}

/* MULTIPLY-only variant: the batch has one blend mode, so the fragment stage needs no
 * mode branch and the vertex stage carries no mode/amount varyings. Same visible result
 * as the general program's `if (mode < 0.5) texel * color` path. */
static unsigned int zcsr_gl_make_inst_program_mul(void) {
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_POS_UV
    /* POS_UV tier: only center / size / UV are uploaded. Tint bakes to white and
     * rotation bakes to zero; there are no loc 4/5 attributes in this tier. */
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_corner;\n"
        "layout(location=1) in vec2 i_center;\n"
        "layout(location=2) in vec2 i_size;\n"
        "layout(location=3) in vec4 i_uv;\n"
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_color;\n"
        "void main(){\n"
        "  vec2 halfsize = i_size * 0.5;\n"
        "  vec2 local = a_corner * halfsize;\n"
        "  vec2 pos = i_center + local;\n"
        "  gl_Position = vec4((pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (pos.y / u_view.y) * 2.0, 0.0, 1.0);\n"
        "  vec2 t = a_corner * 0.5 + 0.5;\n"
        "  v_uv = mix(i_uv.xy, i_uv.zw, t);\n"
        "  v_color = vec4(1.0);\n"
        "}\n";
#elif ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
    /* COMPACT tier: center / size / UV + packed rotation/alpha. The packed
     * rotation expands the corner; tint bakes to white, only alpha survives. */
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_corner;\n"
        "layout(location=1) in vec2 i_center;\n"
        "layout(location=2) in vec2 i_size;\n"
        "layout(location=3) in vec4 i_uv;\n"
        "layout(location=4) in uint i_pack;\n"
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_color;\n"
        "void main(){\n"
        "  vec2 halfsize = i_size * 0.5;\n"
        "  float rot = float(i_pack & 65535u) * 6.28318530718 / 65536.0;\n"
        "  float c = cos(rot);\n"
        "  float s = sin(rot);\n"
        "  vec2 local = a_corner * halfsize;\n"
        "  vec2 pos = i_center + vec2(local.x*c - local.y*s, local.x*s + local.y*c);\n"
        "  gl_Position = vec4((pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (pos.y / u_view.y) * 2.0, 0.0, 1.0);\n"
        "  vec2 t = a_corner * 0.5 + 0.5;\n"
        "  v_uv = mix(i_uv.xy, i_uv.zw, t);\n"
        "  float alpha = float((i_pack >> 16) & 255u) / 255.0;\n"
        "  v_color = vec4(1.0, 1.0, 1.0, alpha);\n"
        "}\n";
#else
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_corner;\n"
        "layout(location=1) in vec2 i_center;\n"
        "layout(location=2) in vec2 i_size;\n"
        "layout(location=3) in vec4 i_uv;\n"
        "layout(location=4) in vec4 i_color;\n"
        "layout(location=5) in float i_rotation;\n"
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_color;\n"
        "void main(){\n"
        "  vec2 halfsize = i_size * 0.5;\n"
        "  float c = cos(i_rotation);\n"
        "  float s = sin(i_rotation);\n"
        "  vec2 local = a_corner * halfsize;\n"
        "  vec2 pos = i_center + vec2(local.x*c - local.y*s, local.x*s + local.y*c);\n"
        "  gl_Position = vec4((pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (pos.y / u_view.y) * 2.0, 0.0, 1.0);\n"
        "  vec2 t = a_corner * 0.5 + 0.5;\n"
        "  v_uv = mix(i_uv.xy, i_uv.zw, t);\n"
        "  v_color = i_color;\n"
        "}\n";
#endif
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "in vec2 v_uv;\n"
        "in vec4 v_color;\n"
        "uniform sampler2D u_tex;\n"
        "out vec4 o_color;\n"
        "vec3 display_to_linear(vec3 c){\n"
        "  c = clamp(c, 0.0, 1.0);\n"
        "  vec3 lo = c / 12.92;\n"
        "  vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));\n"
        "  return mix(lo, hi, step(vec3(0.04045), c));\n"
        "}\n"
        "void main(){\n"
        "  vec4 texel = texture(u_tex, v_uv);\n"
        "  vec4 tint = vec4(display_to_linear(v_color.rgb), v_color.a);\n"
        "  o_color = texel * tint;\n"
        "}\n";
    unsigned int v = zcsr_gl_compile(GL_VERTEX_SHADER, vs);
    unsigned int f = zcsr_gl_compile(GL_FRAGMENT_SHADER, fs);
    unsigned int p;
    int ok = 0;
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    p = glCreateProgram();
    if (!p) { glDeleteShader(v); glDeleteShader(f); return 0; }
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) { glDeleteProgram(p); return 0; }
    return p;
}

/* Active layout tier attribute table (ruling 6). Defined below; used by init. */
static const zcsr_tier_attr* zcsr_active_attrs(size_t* count_out);

/* Plain-only program: a 4-vertex GL_TRIANGLE_STRIP quad, per-instance center /
 * size / UV / rotation / alpha. The record carries no tint color, blend mode,
 * blend amount, recolor, ramp, glow, chroma, emissive or rim input, and the
 * shader declares none. The fragment stage mirrors the general MULTIPLY path
 * with the tint held at white: texel * vec4(1,1,1,a). Built for the FULL /
 * POS_UV tiers only; the COMPACT tier never compiles this program. */
static unsigned int zcsr_gl_make_inst_program_plain(void) {
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_corner;\n"
        "layout(location=1) in vec2 i_center;\n"
        "layout(location=2) in vec2 i_size;\n"
        "layout(location=3) in vec4 i_uv;\n"
        "layout(location=4) in float i_rotation;\n"
        "layout(location=5) in float i_alpha;\n"
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_color;\n"
        "void main(){\n"
        "  vec2 halfsize = i_size * 0.5;\n"
        "  float c = cos(i_rotation);\n"
        "  float s = sin(i_rotation);\n"
        "  vec2 local = a_corner * halfsize;\n"
        "  vec2 pos = i_center + vec2(local.x*c - local.y*s, local.x*s + local.y*c);\n"
        "  gl_Position = vec4((pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (pos.y / u_view.y) * 2.0, 0.0, 1.0);\n"
        "  vec2 t = a_corner * 0.5 + 0.5;\n"
        "  v_uv = mix(i_uv.xy, i_uv.zw, t);\n"
        "  v_color = vec4(1.0, 1.0, 1.0, i_alpha);\n"
        "}\n";
#if ZCSR_COLOR_TIER == ZCSR_COLOR_OPT_LINEAR_HDR
    /* LINEAR_HDR: decode the (white) tint rgb through the same display->linear
     * transfer the general program uses; alpha passes through untouched. */
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "in vec2 v_uv;\n"
        "in vec4 v_color;\n"
        "uniform sampler2D u_tex;\n"
        "out vec4 o_color;\n"
        "vec3 display_to_linear(vec3 c){\n"
        "  c = clamp(c, 0.0, 1.0);\n"
        "  vec3 lo = c / 12.92;\n"
        "  vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));\n"
        "  return mix(lo, hi, step(vec3(0.04045), c));\n"
        "}\n"
        "void main(){\n"
        "  vec4 texel = texture(u_tex, v_uv);\n"
        "  vec4 tint = vec4(display_to_linear(v_color.rgb), v_color.a);\n"
        "  o_color = texel * tint;\n"
        "}\n";
#else
    /* DIRECT_SRGB: slim stage, no linear decode; matches the general DIRECT_SRGB
     * MULTIPLY branch (tint rgb clamped, alpha through). */
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "in vec2 v_uv;\n"
        "in vec4 v_color;\n"
        "uniform sampler2D u_tex;\n"
        "out vec4 o_color;\n"
        "void main(){\n"
        "  vec4 texel = texture(u_tex, v_uv);\n"
        "  vec4 tint = vec4(clamp(v_color.rgb, 0.0, 1.0), v_color.a);\n"
        "  o_color = texel * tint;\n"
        "}\n";
#endif
    unsigned int v = zcsr_gl_compile(GL_VERTEX_SHADER, vs);
    unsigned int f = zcsr_gl_compile(GL_FRAGMENT_SHADER, fs);
    unsigned int p;
    int ok = 0;
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    p = glCreateProgram();
    if (!p) { glDeleteShader(v); glDeleteShader(f); return 0; }
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        zcsr_gl_log_object_info("plain instanced program link failed", p, true);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ===========================================================================================
 * Instanced init / destroy
 * =========================================================================================== */

/* Active ring slot count: persistent 3-slot ring when the opt-in tier probed
 * usable at init, otherwise the original 8-slot DynamicDraw ring. The choice
 * is made once in init; the hot path does not read a per-frame switch. */
static size_t zcsr_inst_slot_count(const zcsr_gl_renderer* r) {
#if ZCSR_INST_PERSISTENT_RING
    return r->inst_persist_ready ? (size_t)ZCSR_INST_PERSISTENT_SLOTS
                                 : (size_t)ZCSR_GL_INSTANCE_SLOTS;
#else
    (void)r;
    return (size_t)ZCSR_GL_INSTANCE_SLOTS;
#endif
}

bool zcsr_gl_instanced_init(zcsr_gl_renderer* r) {
    static const float corners[12] = {
        -1.f, -1.f,   1.f, -1.f,   1.f, 1.f,
        -1.f, -1.f,   1.f, 1.f,   -1.f, 1.f
    };
    static const unsigned short indices[6] = { 0, 1, 2, 0, 2, 3 };
    const size_t stride = zcsr_tier_stride_bytes();
    const zcsr_tier_attr* attrs;
    size_t attr_count, k;

    r->color_ramp_tex = 0;
    r->color_ramp_rows = 0;
    r->inst_program = zcsr_gl_make_inst_program();
    if (!r->inst_program) return false;
    r->inst_uniform_view = glGetUniformLocation(r->inst_program, "u_view");
    r->inst_uniform_tex  = glGetUniformLocation(r->inst_program, "u_tex");
    r->inst_uniform_ramp = glGetUniformLocation(r->inst_program, "u_ramp");
    r->inst_program_mul = zcsr_gl_make_inst_program_mul();
    if (r->inst_program_mul) {
        r->inst_uniform_view_mul = glGetUniformLocation(r->inst_program_mul, "u_view");
        r->inst_uniform_tex_mul  = glGetUniformLocation(r->inst_program_mul, "u_tex");
        /* Ruling 10: every program records its own u_ramp location. The mul
         * shader has no ramp sampler, so this location is expected to be -1
         * (uniform stripped at compile time). The one-time line below records
         * that fact instead of silently relying on unit 0. */
        r->inst_uniform_ramp_mul = glGetUniformLocation(r->inst_program_mul, "u_ramp");
        printf("[glrender] inst_program_mul u_ramp location = %d (negative = stripped)\n",
               r->inst_uniform_ramp_mul);
    } else {
        r->inst_uniform_ramp_mul = -1;
    }

    glGenVertexArrays(1, &r->inst_vao);
    glBindVertexArray(r->inst_vao);

    glGenBuffers(1, &r->inst_corner_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_corner_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glGenBuffers(1, &r->inst_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->inst_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof indices, indices, GL_STATIC_DRAW);

    glGenBuffers(1, &r->inst_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
    z_attrib_i_pointer = (zcsr_pfn_vertex_attrib_i_pointer)
        zcsr_gl_proc_address("glVertexAttribIPointer");
#endif
#if ZCSR_INST_PERSISTENT_RING
    r->inst_persist_ready = false;
    r->inst_persist_map = NULL;
    z_persist_map_range = (zcsr_pfn_map_buffer_range)zcsr_gl_proc_address("glMapBufferRange");
    z_persist_buffer_storage = (zcsr_pfn_buffer_storage)zcsr_gl_proc_address("glBufferStorage");
    z_persist_unmap = (zcsr_pfn_unmap_buffer)zcsr_gl_proc_address("glUnmapBuffer");
    if (zcsr_gl_has_buffer_storage() && z_persist_map_range && z_persist_buffer_storage) {
        size_t persist_size = (size_t)ZCSR_INST_PERSISTENT_SLOTS *
                              ZCSR_GL_MAX_INSTANCES * stride;
        z_persist_buffer_storage(GL_ARRAY_BUFFER, persist_size, NULL,
            GL_DYNAMIC_STORAGE_BIT | GL_MAP_PERSISTENT_BIT |
            GL_MAP_COHERENT_BIT | GL_MAP_WRITE_BIT);
        r->inst_persist_map = z_persist_map_range(GL_ARRAY_BUFFER, 0, persist_size,
            GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_MAP_WRITE_BIT);
        if (r->inst_persist_map) r->inst_persist_ready = true;
    }
    if (!r->inst_persist_ready) {
        /* Clean degrade: drop any incomplete storage and rebuild the original
         * DynamicDraw 8-slot ring. No fake mapped tier, no silent downgrade. */
        if (r->inst_vbo) { glDeleteBuffers(1, &r->inst_vbo); r->inst_vbo = 0; }
        glGenBuffers(1, &r->inst_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (size_t)ZCSR_GL_INSTANCE_SLOTS * ZCSR_GL_MAX_INSTANCES * stride,
                     NULL, GL_DYNAMIC_DRAW);
        printf("[glrender] persistent ring unavailable; default DynamicDraw ring in use\n");
    } else {
        printf("[glrender] persistent ring: %d slots, coherent mapped buffer ready\n",
               ZCSR_INST_PERSISTENT_SLOTS);
    }
#else
    glBufferData(GL_ARRAY_BUFFER,
                 (size_t)ZCSR_GL_INSTANCE_SLOTS * ZCSR_GL_MAX_INSTANCES * stride,
                 NULL, GL_DYNAMIC_DRAW);
#endif
    /* Slot advances once per frame (advance_frame at present/read_rgba); the
     * first frame starts at slot 0 with an empty in-frame byte offset. */
    r->inst_slot = 0;
    r->inst_frame_off = 0;

    /* Attribute slots come from the active layout tier table (ruling 6). The
     * FULL 168 B table preserves every slot the original VAO set up. */
    attrs = zcsr_active_attrs(&attr_count);
    for (k = 0; k < attr_count; ++k) {
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
        zcsr_gl_bind_inst_attr(&attrs[k], (GLsizei)stride);
#else
        glEnableVertexAttribArray(attrs[k].location);
        glVertexAttribPointer(attrs[k].location, attrs[k].count, GL_FLOAT, GL_FALSE,
                              (GLsizei)stride, (void*)attrs[k].offset);
        glVertexAttribDivisor(attrs[k].location, 1);
#endif
    }

    /* Separate VAO for the multiply-only program: it never reads amount/mode,
     * and keeping those arrays enabled makes some drivers fetch per-instance
     * data the shader cannot use. Same VBOs, no duplicate storage. */
    if (r->inst_program_mul) {
        glGenVertexArrays(1, &r->inst_vao_mul);
        glBindVertexArray(r->inst_vao_mul);
        glBindBuffer(GL_ARRAY_BUFFER, r->inst_corner_vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->inst_ebo);
        glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
        for (k = 0; k < attr_count && attrs[k].location <= 5; ++k) {
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
            zcsr_gl_bind_inst_attr(&attrs[k], (GLsizei)stride);
#else
            glEnableVertexAttribArray(attrs[k].location);
            glVertexAttribPointer(attrs[k].location, attrs[k].count, GL_FLOAT, GL_FALSE,
                                  (GLsizei)stride, (void*)attrs[k].offset);
            glVertexAttribDivisor(attrs[k].location, 1);
#endif
        }
    }

#if ZCSR_INSTANCE_LAYOUT != ZCSR_LAYOUT_COMPACT
    /* Plain entry: own program / VAO / corner VBO / instance VBO. Separate
     * attribute slots in a separate VAO; there is no shared state with the
     * general instanced VAO. FULL / POS_UV tiers only. */
    {
        static const float plain_corners[8] = {
            -1.f, -1.f,   1.f, -1.f,   -1.f, 1.f,   1.f, 1.f
        };
        const size_t plain_stride = sizeof(zcsr_gl_instance_plain);
        r->plain_program = zcsr_gl_make_inst_program_plain();
        if (!r->plain_program) return false;
        r->plain_uniform_view = glGetUniformLocation(r->plain_program, "u_view");
        r->plain_uniform_tex  = glGetUniformLocation(r->plain_program, "u_tex");

        glGenVertexArrays(1, &r->plain_vao);
        glBindVertexArray(r->plain_vao);

        glGenBuffers(1, &r->plain_corner_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, r->plain_corner_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof plain_corners, plain_corners, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

        glGenBuffers(1, &r->plain_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, r->plain_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (size_t)ZCSR_GL_INSTANCE_SLOTS * ZCSR_GL_MAX_INSTANCES * plain_stride,
                     NULL, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)plain_stride, (void*)0);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)plain_stride, (void*)8);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, (GLsizei)plain_stride, (void*)16);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, (GLsizei)plain_stride, (void*)32);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, (GLsizei)plain_stride, (void*)36);
        glVertexAttribDivisor(1, 1);
        glVertexAttribDivisor(2, 1);
        glVertexAttribDivisor(3, 1);
        glVertexAttribDivisor(4, 1);
        glVertexAttribDivisor(5, 1);

        r->plain_slot = 0;
        r->plain_frame_off = 0;
    }
#endif

    glBindVertexArray(0);
    return r->inst_vao != 0 && r->inst_vbo != 0;
}

/* Frame boundary: rotate both instance rings to the next slot and reset the
 * in-frame byte offsets. Called only from zcsr_gl_present / zcsr_gl_read_rgba;
 * draw entries never advance the slot themselves. The state cache mirror is
 * invalidated so the next frame rebinds program / VAO / texture / viewport
 * against whatever the frame-end pass left bound. */
void zcsr_gl_inst_advance_frame(zcsr_gl_renderer* r) {
    if (!r) return;
    r->inst_slot = (r->inst_slot + 1u) % (unsigned int)zcsr_inst_slot_count(r);
    r->inst_frame_off = 0;
#if ZCSR_INSTANCE_LAYOUT != ZCSR_LAYOUT_COMPACT
    r->plain_slot = (r->plain_slot + 1u) % (unsigned int)ZCSR_GL_INSTANCE_SLOTS;
    r->plain_frame_off = 0;
#endif
    zcsr_gl_state_cache_invalidate(r);
}

void zcsr_gl_instanced_destroy(zcsr_gl_renderer* r) {
#if ZCSR_INST_PERSISTENT_RING
    if (r->inst_persist_ready && r->inst_persist_map && z_persist_unmap) {
        glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
        z_persist_unmap(GL_ARRAY_BUFFER);
    }
#endif
    if (r->inst_vbo) glDeleteBuffers(1, &r->inst_vbo);
    if (r->inst_ebo) glDeleteBuffers(1, &r->inst_ebo);
    if (r->inst_corner_vbo) glDeleteBuffers(1, &r->inst_corner_vbo);
    if (r->inst_vao) glDeleteVertexArrays(1, &r->inst_vao);
    if (r->inst_vao_mul) glDeleteVertexArrays(1, &r->inst_vao_mul);
    if (r->inst_program) glDeleteProgram(r->inst_program);
    if (r->inst_program_mul) glDeleteProgram(r->inst_program_mul);
#if ZCSR_INSTANCE_LAYOUT != ZCSR_LAYOUT_COMPACT
    if (r->plain_vbo) glDeleteBuffers(1, &r->plain_vbo);
    if (r->plain_corner_vbo) glDeleteBuffers(1, &r->plain_corner_vbo);
    if (r->plain_vao) glDeleteVertexArrays(1, &r->plain_vao);
    if (r->plain_program) glDeleteProgram(r->plain_program);
#endif
}

/* ===========================================================================================
 * Public API: draw_instances
 * =========================================================================================== */

/* Active layout tier attribute table (ruling 6). The struct itself is unchanged;
 * the tier decides how many slots and what stride the VAO exposes. */
static const zcsr_tier_attr* zcsr_active_attrs(size_t* count_out) {
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_POS_UV
    *count_out = ZCSR_POS_UV_ATTR_COUNT;
    return zcsr_pos_uv_attrs;
#elif ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
    *count_out = ZCSR_COMPACT_ATTR_COUNT;
    return zcsr_compact_attrs;
#else
    *count_out = ZCSR_FULL_ATTR_COUNT;
    return zcsr_full_attrs;
#endif
}

/* ---- GL state cache mirror (instanced hot path only) -------------------------
 * The mirror records the last program / VAO / unit-0 texture / unit-1 texture /
 * viewport the renderer applied. A matching GL call is skipped only when the
 * mirror is valid and the recorded id (or viewport dims) still matches; any
 * external entry that rebinds GL state invalidates the mirror through
 * zcsr_gl_state_cache_invalidate(). */
static void inst_cache_program(zcsr_gl_renderer* r, unsigned int program,
                                int uni_view, int uni_tex, int uni_ramp) {
    const bool same = r->st_cache_valid && r->st_cache_program == program &&
                      r->st_cache_vp_w == (int)r->width && r->st_cache_vp_h == (int)r->height;
    if (same) return;
    glUseProgram(program);
    glUniform2f(uni_view, (float)r->width, (float)r->height);
    glUniform1i(uni_tex, 0);
    if (uni_ramp >= 0) glUniform1i(uni_ramp, 1);
    r->st_cache_program = program;
    r->st_cache_vp_w = (int)r->width;
    r->st_cache_vp_h = (int)r->height;
    r->st_cache_valid = true;
}

static void inst_cache_viewport(zcsr_gl_renderer* r) {
    if (r->st_cache_valid && r->st_cache_vp_w == (int)r->width &&
        r->st_cache_vp_h == (int)r->height) return;
    glViewport(0, 0, r->width, r->height);
    r->st_cache_vp_w = (int)r->width;
    r->st_cache_vp_h = (int)r->height;
    r->st_cache_valid = true;
}

static void inst_cache_vao(zcsr_gl_renderer* r, unsigned int vao) {
    if (r->st_cache_valid && r->st_cache_vao == vao) return;
    glBindVertexArray(vao);
    r->st_cache_vao = vao;
    r->st_cache_valid = true;
}

static void inst_cache_tex0(zcsr_gl_renderer* r, unsigned int tex) {
    glActiveTexture(GL_TEXTURE0);
    if (r->st_cache_valid && r->st_cache_tex0 == tex) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    r->st_cache_tex0 = tex;
    r->st_cache_valid = true;
}

static void inst_cache_tex1(zcsr_gl_renderer* r, unsigned int tex) {
    glActiveTexture(GL_TEXTURE0 + 1);
    if (r->st_cache_valid && r->st_cache_tex1 == tex) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    r->st_cache_tex1 = tex;
    r->st_cache_valid = true;
}

/* Point the current slot's VAO at its slice of the ring buffer. Called once per
 * uploaded batch; all batches of one frame share the same slot and differ only
 * by the in-frame byte offset. */
static void zcsr_gl_inst_bind_slot_attribs(zcsr_gl_renderer* r, size_t base) {
    const size_t stride = zcsr_tier_stride_bytes();
    const zcsr_tier_attr* attrs;
    size_t attr_count, k;
    inst_cache_vao(r, r->inst_current_mul && r->inst_vao_mul ? r->inst_vao_mul : r->inst_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
    attrs = zcsr_active_attrs(&attr_count);
    for (k = 0; k < attr_count; ++k) {
        if (r->inst_current_mul && attrs[k].location > 5) continue;
        glEnableVertexAttribArray(attrs[k].location);
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
        const void* off = (void*)(base + attrs[k].offset);
        if (attrs[k].type == ZCSR_ATTR_UINT) {
            if (z_attrib_i_pointer) {
                z_attrib_i_pointer(attrs[k].location, attrs[k].count, GL_UNSIGNED_INT,
                                   (GLsizei)stride, off);
            }
        } else {
            glVertexAttribPointer(attrs[k].location, attrs[k].count, GL_FLOAT, GL_FALSE,
                                  (GLsizei)stride, off);
        }
#else
        glVertexAttribPointer(attrs[k].location, attrs[k].count, GL_FLOAT, GL_FALSE,
                              (GLsizei)stride, (void*)(base + attrs[k].offset));
#endif
        glVertexAttribDivisor(attrs[k].location, 1);
    }
}

#if ZCSR_INSTANCE_LAYOUT != ZCSR_LAYOUT_COMPACT
/* Plain ring: re-point the five per-instance arrays at this batch's slot slice.
 * The plain VAO stores the corner attrib; the instance VBO base moves with the
 * in-frame byte offset, so the pointers must be rebound to `base` each batch. */
static void zcsr_gl_plain_bind_slot_attribs(zcsr_gl_renderer* r, size_t base) {
    const size_t stride = sizeof(zcsr_gl_instance_plain);
    inst_cache_vao(r, r->plain_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->plain_vbo);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(base + 0));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(base + 8));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(base + 16));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(base + 32));
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(base + 36));
}
#endif

#if ZCSR_INSTANCE_LAYOUT != ZCSR_LAYOUT_COMPACT
/* Generic-entry upload helper (FULL / POS_UV tiers only). The COMPACT tier has
 * its own entry that gathers 36 B records directly; it never calls this
 * staging function and never converts a 168 B record. */
static void zcsr_gl_draw_instances_prepared(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                             const zcsr_gl_instance* instances, size_t count) {
    const size_t stride = zcsr_tier_stride_bytes();
    const size_t slot_count = zcsr_inst_slot_count(r);
    size_t offset;
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_POS_UV
    /* Compact 32 B staging: the VBO never holds the full 168 B descriptor. */
    static zcsr_instance_pos_uv pos_stage[ZCSR_GL_MAX_INSTANCES];
    const void* upload_src;
    size_t k;
    for (k = 0; k < count; ++k) {
        pos_stage[k].cx = instances[k].cx;
        pos_stage[k].cy = instances[k].cy;
        pos_stage[k].w  = instances[k].w;
        pos_stage[k].h  = instances[k].h;
        pos_stage[k].u0 = instances[k].u0;
        pos_stage[k].v0 = instances[k].v0;
        pos_stage[k].u1 = instances[k].u1;
        pos_stage[k].v1 = instances[k].v1;
    }
    upload_src = pos_stage;
#else
    const void* upload_src = instances;
#endif
    {
        const size_t slot_cap = (size_t)ZCSR_GL_MAX_INSTANCES * stride;
        const size_t need = count * stride;
        if (r->inst_skip_upload) {
            /* Diagnostic reuse: keep the slot filled before uploads were
             * disabled. No slot advance; the stale slot-0 prefix is drawn
             * exactly as before, so the in-slot offset is reset to the prefix. */
            r->inst_slot = 0;
            r->inst_frame_off = 0;
            offset = 0;
        } else {
            if (r->inst_frame_off + need > slot_cap) {
                /* Mid-frame wrap: this frame's retained count exceeds one slot
                 * (only possible when count > ZCSR_GL_MAX_INSTANCES). Rotate to
                 * the next slot, reset the in-slot offset, and log instead of
                 * going silent. */
                r->inst_slot = (r->inst_slot + 1u) % (unsigned int)slot_count;
                r->inst_frame_off = 0;
                printf("[glrender] instance ring wrapped mid-frame: slot=%u chunk=%zu bytes\n",
                       r->inst_slot, need);
            }
            offset = (size_t)r->inst_slot * slot_cap + r->inst_frame_off;
            r->inst_frame_off += need;
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
#if ZCSR_INST_PERSISTENT_RING
    if (!r->inst_persist_ready)
#endif
    {
#ifdef ZCSR_GL_VBO_ORPHAN
        /* Orphaning: discard old storage before partial upload to avoid GPU sync stall. */
        glBufferData(GL_ARRAY_BUFFER,
                     (size_t)ZCSR_GL_INSTANCE_SLOTS * ZCSR_GL_MAX_INSTANCES * stride, NULL,
                     GL_DYNAMIC_DRAW);
#endif
        if (r->inst_orphan) {
            glBufferData(GL_ARRAY_BUFFER,
                         (size_t)ZCSR_GL_INSTANCE_SLOTS * ZCSR_GL_MAX_INSTANCES * stride, NULL,
                         GL_STREAM_DRAW);
        }
    }
    if (!r->inst_skip_upload) {
#if ZCSR_INST_PERSISTENT_RING
        if (r->inst_persist_ready) {
            /* Persistent coherent mapping: write the slot straight into the
             * resident mapped range; no per-batch BufferSubData driver copy and
             * no implicit sync (coherent visibility). */
            memcpy((char*)r->inst_persist_map + offset, upload_src, count * stride);
            ++r->stat_inst_map_writes;
        } else
#endif
        {
            glBufferSubData(GL_ARRAY_BUFFER, offset, count * stride, upload_src);
            ++r->stat_inst_upload_calls;   /* ruling 9: BufferSubData calls on the active ring */
        }
    }
    zcsr_gl_inst_bind_slot_attribs(r, offset);
    inst_cache_tex0(r, texture);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)count);
}
#endif

#if ZCSR_INSTANCE_LAYOUT != ZCSR_LAYOUT_COMPACT
/* Set up the shared instanced GL state once for a sequence of texture runs.
 * Generic (FULL / POS_UV) entry only; the COMPACT entry has its own state setup. */
static bool zcsr_gl_instanced_begin_state(zcsr_gl_renderer* r, bool multiply_only) {
    unsigned int program;
    int uniform_view, uniform_tex, uniform_ramp;
    if (!r) return false;
    if (!zcsr_gl_make_current(r)) return false;
    if (!zcsr_gl_bind_active_target(r)) return false;
    inst_cache_viewport(r);
    if (multiply_only && r->inst_program_mul) {
        program = r->inst_program_mul;
        uniform_view = r->inst_uniform_view_mul;
        uniform_tex = r->inst_uniform_tex_mul;
        uniform_ramp = r->inst_uniform_ramp_mul;   /* ruling 10: bind whichever program */
    } else {
        program = r->inst_program;
        uniform_view = r->inst_uniform_view;
        uniform_tex = r->inst_uniform_tex;
        uniform_ramp = r->inst_uniform_ramp;
    }
    inst_cache_program(r, program, uniform_view, uniform_tex, uniform_ramp);
    inst_cache_tex1(r, r->color_ramp_tex);
    glActiveTexture(GL_TEXTURE0);
    r->inst_current_mul = (program == r->inst_program_mul);
    return true;
}
#endif

static void zcsr_gl_instanced_end_state(zcsr_gl_renderer* r) {
    glBindVertexArray(0);
    /* Mirror the VAO unbind: program / texture / viewport stay bound. */
    if (r) r->st_cache_vao = 0;
}

#if ZCSR_INSTANCE_LAYOUT != ZCSR_LAYOUT_COMPACT
#if ZCSR_DEBUG_VALIDATE
/* Write-side sanitize: finite + range checks for one instance. Compiled only
 * when ZCSR_DEBUG_VALIDATE is non-zero; a Release build omits this entire block,
 * so the shipping path carries no per-instance isfinite scan over the 168 B
 * struct. Validation runs once per instance at the submission boundary, not on
 * every draw pass. */
static bool zcsr_gl_instance_sanitize(const zcsr_gl_instance* in) {
    if (!in) return false;
    if (!isfinite(in->cx) || !isfinite(in->cy) || !isfinite(in->w) || !isfinite(in->h) ||
        !isfinite(in->u0) || !isfinite(in->v0) || !isfinite(in->u1) || !isfinite(in->v1) ||
        !isfinite(in->r) || !isfinite(in->g) || !isfinite(in->b) || !isfinite(in->a) ||
        !isfinite(in->rotation) || !isfinite(in->amount) || !isfinite(in->mode) ||
        !isfinite(in->recolor.hue_source) || !isfinite(in->recolor.hue_target) ||
        !isfinite(in->recolor.hue_width) || !isfinite(in->recolor.hue_amount) ||
        !isfinite(in->recolor.ramp_row) || !isfinite(in->recolor.ramp_amount) ||
        !isfinite(in->recolor.ramp_mode) || !isfinite(in->recolor.ramp_luma) ||
        !isfinite(in->recolor.glow_r) || !isfinite(in->recolor.glow_g) ||
        !isfinite(in->recolor.glow_b) || !isfinite(in->recolor.glow_amount) ||
        !isfinite(in->recolor.sat_boost) || !isfinite(in->recolor.sat_floor) ||
        !isfinite(in->recolor.value_gain) || !isfinite(in->recolor.value_contrast) ||
        !isfinite(in->recolor.emiss_r) || !isfinite(in->recolor.emiss_g) ||
        !isfinite(in->recolor.emiss_b) || !isfinite(in->recolor.emiss_amount) ||
        !isfinite(in->recolor.rim_r) || !isfinite(in->recolor.rim_g) ||
        !isfinite(in->recolor.rim_b) || !isfinite(in->recolor.rim_amount) ||
        !isfinite(in->z)) return false;
    if (in->recolor.hue_amount < 0.0f || in->recolor.hue_amount > 1.0f ||
        in->recolor.ramp_amount < 0.0f || in->recolor.ramp_amount > 1.0f ||
        in->recolor.ramp_mode < 0.0f || in->recolor.ramp_mode > 1.0f ||
        in->recolor.ramp_luma < 0.0f || in->recolor.ramp_luma > 1.0f ||
        in->recolor.glow_amount < 0.0f || in->recolor.glow_amount > 1.0f ||
        in->recolor.sat_boost < 0.0f || in->recolor.sat_boost > 1.0f ||
        in->recolor.sat_floor < 0.0f || in->recolor.sat_floor > 1.0f ||
        in->recolor.value_gain < -1.0f || in->recolor.value_gain > 1.0f ||
        in->recolor.value_contrast < -1.0f || in->recolor.value_contrast > 1.0f ||
        in->recolor.emiss_amount < 0.0f || in->recolor.emiss_amount > 4.0f ||
        in->recolor.rim_amount < 0.0f || in->recolor.rim_amount > 2.0f) return false;
    if (in->recolor.hue_amount > 0.0f && in->recolor.hue_width < 0.0f) return false;
    if (in->recolor.ramp_amount > 0.0f && in->recolor.ramp_row < 0.0f) return false;
    if (in->recolor.glow_r < 0.0f || in->recolor.glow_r > 1.0f ||
        in->recolor.glow_g < 0.0f || in->recolor.glow_g > 1.0f ||
        in->recolor.glow_b < 0.0f || in->recolor.glow_b > 1.0f) return false;
    if (in->recolor.emiss_r < 0.0f || in->recolor.emiss_r > 1.0f ||
        in->recolor.emiss_g < 0.0f || in->recolor.emiss_g > 1.0f ||
        in->recolor.emiss_b < 0.0f || in->recolor.emiss_b > 1.0f) return false;
    if (in->recolor.rim_r < 0.0f || in->recolor.rim_r > 1.0f ||
        in->recolor.rim_g < 0.0f || in->recolor.rim_g > 1.0f ||
        in->recolor.rim_b < 0.0f || in->recolor.rim_b > 1.0f) return false;
    return in->w > 0.0f && in->h > 0.0f;
}
#endif

/* Multiply-only eligibility: a batch can take the slim mul program only when
 * every instance is plain multiply with no recolor stage active. */
static bool zcsr_gl_instance_is_mul(const zcsr_gl_instance* in) {
    return in->mode == (float)ZCSR_MOD_MULTIPLY &&
           in->recolor.hue_amount <= 0.0f &&
           in->recolor.ramp_amount <= 0.0f &&
           in->recolor.glow_amount <= 0.0f &&
           in->recolor.sat_boost <= 0.0f &&
           in->recolor.sat_floor <= 0.0f &&
           fabsf(in->recolor.value_gain) <= 0.0f &&
           fabsf(in->recolor.value_contrast) <= 0.0f &&
           in->recolor.emiss_amount <= 0.0f &&
           in->recolor.rim_amount <= 0.0f;
}

/* Ruling 6 / C2: POS_UV compiles only center / size / UV. The only compatible
 * entry is a plain axis-aligned white multiply with no rotation, blend amount,
 * tint or recolor stage — anything the 32 B record does not carry must fail loud. */
static bool zcsr_gl_instance_pos_uv_compatible(const zcsr_gl_instance* in) {
    return zcsr_gl_instance_is_mul(in) &&
           in->rotation == 0.0f &&
           in->amount == 0.0f &&
           in->r == 1.0f && in->g == 1.0f && in->b == 1.0f && in->a == 1.0f;
}

/* Ruling 8 / B2: any optional stage the DIRECT_SRGB slim fragment does not
 * compile (hue / ramp / glow / chroma conditioning / emiss / rim). */
static bool zcsr_gl_instance_recolor_active(const zcsr_gl_instance* in) {
    return in->recolor.hue_amount > 0.0f ||
           in->recolor.ramp_amount > 0.0f ||
           in->recolor.glow_amount > 0.0f ||
           in->recolor.sat_boost > 0.0f ||
           in->recolor.sat_floor > 0.0f ||
           in->recolor.value_gain != 0.0f ||
           in->recolor.value_contrast != 0.0f ||
           in->recolor.emiss_amount > 0.0f ||
           in->recolor.rim_amount > 0.0f;
}

/* Ruling 2 predicate: the quad lies completely outside the current viewport
 * (or has no area). Reads only the 32 B prefix fields. */
static bool zcsr_gl_inst_offscreen(const zcsr_gl_renderer* r, const zcsr_gl_instance* in) {
    float l, t, rr, b;
    if (in->w <= 0.0f || in->h <= 0.0f) return true;
    l = in->cx - in->w * 0.5f;
    t = in->cy - in->h * 0.5f;
    rr = l + in->w;
    b = t + in->h;
    return l >= (float)r->width || rr <= 0.0f ||
           t >= (float)r->height || b <= 0.0f;
}

/* Pass 1 (write-side gate): classify inside the existing select traversal —
 * the dead / offscreen / alpha0 counters are accumulated here with no second
 * scan. Retained indices go to the 4 B index array and the culling bitmap.
 * *out_mul receives the multiply-only classification; *out_skipped the rejected
 * count. Returns the retained count. */
static size_t zcsr_gl_select_instances(zcsr_gl_renderer* r,
                                       const zcsr_gl_instance* instances,
                                       size_t count,
                                       bool* out_mul, size_t* out_skipped) {
    size_t retained = 0, skipped = 0, i;
    size_t dead = 0, offscreen = 0, alpha0 = 0;
    bool mul = true;
    float culled_ratio;
    if (count > ZCSR_CULL_BITMAP_CAPACITY) count = ZCSR_CULL_BITMAP_CAPACITY;
    zcsr_bitmap_clear(r->inst_vis, ZCSR_CULL_BITMAP_CAPACITY);
    for (i = 0; i < count; ++i) {
        const zcsr_gl_instance* in = &instances[i];
        bool drop = false;
#if ZCSR_DEBUG_VALIDATE
        if (!zcsr_gl_instance_sanitize(in)) {
            ++dead;
            ++skipped;
            drop = true;
        }
#endif
        /* Error-path consistency: a ramp-weighted instance with no atlas bound
         * cannot sample a valid row. Skip that instance; never drop the frame. */
        if (!drop && in->recolor.ramp_amount > 0.0f && r->color_ramp_tex == 0u) {
            ++dead;
            ++skipped;
            drop = true;
        }
#if ZCSR_COLOR_TIER == ZCSR_COLOR_OPT_DIRECT_SRGB
        /* B2: the slim DIRECT_SRGB fragment compiles no recolor/ramp/glow/
         * emiss/rim stage. Reject such an instance with a one-time ERROR
         * instead of silently drawing the untransformed (wrong) color. The
         * whole block is compiled out in the LINEAR_HDR default tier. */
        if (!drop && zcsr_gl_instance_recolor_active(in)) {
            ++dead;
            ++skipped;
            drop = true;
            ZCSR_LOG_ONCE(ZCSR_LOG_ERROR, "glrender",
                "DIRECT_SRGB tier: recolor/ramp/emiss/rim stage is not compiled; instance rejected (no silent recolor)");
        }
#endif
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_POS_UV
        /* C2: the 32 B record carries no mode/amount/tint/rotation/recolor.
         * Reject such an instance with a one-time ERROR instead of silently
         * dropping the transform or drawing a white-wrong quad. Compiled out
         * in the FULL default tier. */
        if (!drop && !zcsr_gl_instance_pos_uv_compatible(in)) {
            ++dead;
            ++skipped;
            drop = true;
            ZCSR_LOG_ONCE(ZCSR_LOG_ERROR, "glrender",
                "POS_UV tier: mode/amount/tint/rotation/recolor stage is not compiled; instance rejected (no silent drop)");
        }
#endif
        if (!drop && zcsr_gl_inst_offscreen(r, in)) ++offscreen;
        if (!drop && in->a <= 0.0f) ++alpha0;
        if (drop) continue;
        if (!zcsr_gl_instance_is_mul(in)) mul = false;
        r->inst_sel[retained++] = (uint32_t)i;
        zcsr_bitmap_set(r->inst_vis, i);
    }
    r->stat_inst_scanned += count;
    r->stat_inst_skipped += skipped;
    r->stat_inst_dead += dead;
    r->stat_inst_offscreen += offscreen;
    r->stat_inst_alpha0 += alpha0;

    /* Gate (ruling 2): single threshold macro, unlikely branch, no per-frame
     * bool switch. Below the ratio the bitmap path keeps every live candidate;
     * at/above it the index-only full-cull sweep drops offscreen / alpha0
     * candidates in a second pass that walks only the 4 B index array. */
    culled_ratio = (count > 0u)
        ? (float)(dead + offscreen + alpha0) / (float)count : 0.0f;
    if (ZCSR_UNLIKELY(culled_ratio >= ZCSR_CULL_FULL_SCAN_RATIO)) {
        size_t w = 0, k;
        r->stat_inst_cull_path = 1;
        for (k = 0; k < retained; ++k) {
            const zcsr_gl_instance* q = &instances[r->inst_sel[k]];
            if (zcsr_gl_inst_offscreen(r, q) || q->a <= 0.0f) continue;
            r->inst_sel[w++] = r->inst_sel[k];
        }
        retained = w;
    } else {
        r->stat_inst_cull_path = 0;
    }

    if (out_mul) *out_mul = mul;
    if (out_skipped) *out_skipped = skipped;
    if (skipped > 0u) {
        /* One-time warning: static atomic latch at this call site, so a bad
         * batch never floods the log frame after frame. */
        ZCSR_LOG_ONCE(ZCSR_LOG_WARN, "glrender",
                      "draw_instances: %zu instance(s) skipped (invalid / unbound ramp); frame not dropped",
                      skipped);
    }
    return retained;
}

/* Pass 2: walk the 4 B index array and gather one upload chunk at a time. The
 * second pass touches only uint32 indices (4 B each); it never re-scans the
 * 168 B instance structs. */
static size_t zcsr_gl_draw_selected(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                     const zcsr_gl_instance* instances,
                                     size_t retained) {
    size_t done = 0, submitted = 0;
    if (!r || !instances || retained == 0 || texture == 0) return 0;
    if (!zcsr_gl_tex_path_a_compatible(r, texture)) return 0;
    while (done < retained) {
        size_t n = retained - done;
        size_t k;
        if (n > (size_t)ZCSR_GL_MAX_INSTANCES) n = (size_t)ZCSR_GL_MAX_INSTANCES;
        for (k = 0; k < n; ++k) {
            r->inst_stage[k] = instances[r->inst_sel[done + k]];
        }
        r->stat_inst_upload_bytes += n * zcsr_tier_stride_bytes();
        zcsr_gl_draw_instances_prepared(r, texture, r->inst_stage, n);
        submitted += n;
        done += n;
    }
    r->stat_inst_touch_bytes += retained * sizeof(uint32_t);
    return submitted;
}

/* Draw one texture run, splitting it into upload-cap-sized chunks. Shared state
 * (framebuffer/program/VAO) is already set by the caller. Returns submitted count. */
static size_t zcsr_gl_draw_run_batched(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                       const zcsr_gl_instance* instances, size_t count) {
    size_t retained;
    if (!r || !instances || count == 0 || texture == 0) return 0;
    if (!zcsr_gl_tex_path_a_compatible(r, texture)) return 0;
    retained = zcsr_gl_select_instances(r, instances, count, NULL, NULL);
    return zcsr_gl_draw_selected(r, texture, instances, retained);
}

size_t zcsr_gl_draw_instances(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                              const zcsr_gl_instance* instances, size_t count) {
    size_t submitted, retained;
    bool multiply_only;
    if (!r || !instances || count == 0 || texture == 0) return 0;
    retained = zcsr_gl_select_instances(r, instances, count, &multiply_only, NULL);
    if (!zcsr_gl_instanced_begin_state(r, multiply_only)) return 0;
    submitted = zcsr_gl_draw_selected(r, texture, instances, retained);
    zcsr_gl_instanced_end_state(r);
    return submitted;
}

/* No-cull sibling entry: the caller guarantees all `count` instances are
 * visible. No select, no per-instance bitmap test, no gather/reorder — the
 * 168 B records are uploaded consecutively and drawn in submission order. It
 * reuses the same slot/orphan/upload/bind/draw state machine as the generic
 * entry (zcsr_gl_draw_instances_prepared); there is no second copy of that
 * state machine and no extra entry parameter. The game chooses which of the
 * two entries to call; the engine never routes between them. */
size_t zcsr_gl_draw_instances_visible(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                      const zcsr_gl_instance* instances, size_t count) {
    size_t done = 0, submitted = 0;
    if (!r || !instances || count == 0 || texture == 0) return 0;
    if (!zcsr_gl_tex_path_a_compatible(r, texture)) return 0;
    if (!zcsr_gl_instanced_begin_state(r, false)) return 0;
    while (done < count) {
        size_t n = count - done;
        if (n > (size_t)ZCSR_GL_MAX_INSTANCES) n = (size_t)ZCSR_GL_MAX_INSTANCES;
        r->stat_inst_upload_bytes += n * zcsr_tier_stride_bytes();
        zcsr_gl_draw_instances_prepared(r, texture, instances + done, n);
        submitted += n;
        done += n;
    }
    zcsr_gl_instanced_end_state(r);
    return submitted;
}

/* Shared GL state for the plain entry: make current / active target / viewport /
 * plain program / uniforms. No ramp sampler and no second texture unit. */
static bool zcsr_gl_plain_begin_state(zcsr_gl_renderer* r) {
    if (!r) return false;
    if (!zcsr_gl_make_current(r)) return false;
    if (!zcsr_gl_bind_active_target(r)) return false;
    inst_cache_viewport(r);
    inst_cache_program(r, r->plain_program, r->plain_uniform_view,
                       r->plain_uniform_tex, -1);
    glActiveTexture(GL_TEXTURE0);
    return true;
}

/* No plain_run / chunk entry: measured 65 draw of 1,023 against 1 draw of
 * 65,535 was equal (0.835 vs 0.833 ms), so splitting has zero benefit and
 * negative ROI. One call carries the whole range; the only split left is the
 * internal ZCSR_GL_MAX_INSTANCES chunking above, which the caller cannot tune. */

/* Plain instanced entry: 4-vertex GL_TRIANGLE_STRIP, per-instance center /
 * size / UV / rotation / alpha. No cull, no select, no bitmap test, no
 * gather/reorder, no sort. Records are uploaded consecutively into the plain
 * ring and drawn in submission order. A batch larger than
 * ZCSR_GL_MAX_INSTANCES splits into consecutive chunks; no record is dropped. */
size_t zcsr_gl_draw_instances_plain(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                    const zcsr_gl_instance_plain* records, size_t count) {
    const size_t stride = sizeof(zcsr_gl_instance_plain);
    size_t done = 0, submitted = 0;
    if (!r || !records || count == 0 || texture == 0) return 0;
    if (!zcsr_gl_tex_path_a_compatible(r, texture)) return 0;
    if (!zcsr_gl_plain_begin_state(r)) return 0;
    {
        const size_t plain_slot_cap = (size_t)ZCSR_GL_MAX_INSTANCES * stride;
        while (done < count) {
            size_t n = count - done;
            size_t base;
            size_t need;
            if (n > (size_t)ZCSR_GL_MAX_INSTANCES) n = (size_t)ZCSR_GL_MAX_INSTANCES;
            need = n * stride;
            if (r->inst_skip_upload) {
                /* Diagnostic reuse: keep the slot filled before uploads were
                 * disabled. No slot advance; stale slot-0 prefix, offset zero. */
                r->plain_slot = 0;
                r->plain_frame_off = 0;
                base = 0;
            } else {
                if (r->plain_frame_off + need > plain_slot_cap) {
                    /* Mid-frame wrap: this frame exceeds one plain slot (only
                     * possible when count > ZCSR_GL_MAX_INSTANCES). Rotate, reset
                     * the offset, and log instead of going silent. */
                    r->plain_slot = (r->plain_slot + 1u) % (unsigned int)ZCSR_GL_INSTANCE_SLOTS;
                    r->plain_frame_off = 0;
                    printf("[glrender] plain instance ring wrapped mid-frame: slot=%u chunk=%zu bytes\n",
                           r->plain_slot, need);
                }
                base = (size_t)r->plain_slot * plain_slot_cap + r->plain_frame_off;
                r->plain_frame_off += need;
            }
            glBindBuffer(GL_ARRAY_BUFFER, r->plain_vbo);
            if (!r->inst_skip_upload) {
                glBufferSubData(GL_ARRAY_BUFFER, base, need, records + done);
            }
            zcsr_gl_plain_bind_slot_attribs(r, base);
            inst_cache_tex0(r, texture);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)n);
            submitted += n;
            done += n;
        }
    }
    zcsr_gl_instanced_end_state(r);
    return submitted;
}

#endif /* generic (FULL / POS_UV) entry chain */

/* ===========================================================================================
 * COMPACT tier entry: zcsr_gl_draw_instances_compact
 *
 * Independent 36 B path: its own cull/select, its own same-format gather, its
 * own upload and draw. It does not call zcsr_gl_draw_instances, the generic
 * prepared staging helper, or any 168 B record accessor. There is no 168 -> 36
 * conversion on this tier: the caller supplies packed 36 B records directly.
 * =========================================================================================== */
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT

/* Offscreen / zero-area test over a 36 B record: reads only cx, cy, w, h. */
static bool zcsr_gl_compact_offscreen(const zcsr_gl_renderer* r,
                                      const zcsr_instance_compact* in) {
    float l, t, rr, b;
    if (in->w <= 0.0f || in->h <= 0.0f) return true;
    l = in->cx - in->w * 0.5f;
    t = in->cy - in->h * 0.5f;
    rr = l + in->w;
    b = t + in->h;
    return l >= (float)r->width || rr <= 0.0f ||
           t >= (float)r->height || b <= 0.0f;
}

static uint32_t zcsr_gl_compact_alpha8(const zcsr_instance_compact* in) {
    return (in->pack >> ZCSR_COMPACT_PACK_ALPHA_SHIFT) & ZCSR_COMPACT_PACK_ALPHA_MASK;
}

/* Pass 1: cull/select over the 36 B records. Retained indices go to the 4 B
 * index array and the culling bitmap; the second pass walks only indices.
 * Counters keep the same semantics as the generic select; upload_bytes is
 * counted per 36 B record at upload time. */
static size_t zcsr_gl_compact_select(zcsr_gl_renderer* r,
                                     const zcsr_instance_compact* records,
                                     size_t count) {
    size_t retained = 0, i;
    size_t offscreen = 0, alpha0 = 0;
    float culled_ratio;
    if (count > ZCSR_CULL_BITMAP_CAPACITY) count = ZCSR_CULL_BITMAP_CAPACITY;
    zcsr_bitmap_clear(r->inst_vis, ZCSR_CULL_BITMAP_CAPACITY);
    for (i = 0; i < count; ++i) {
        const zcsr_instance_compact* in = &records[i];
        if (zcsr_gl_compact_offscreen(r, in)) ++offscreen;
        if (zcsr_gl_compact_alpha8(in) == 0u) ++alpha0;
        r->inst_sel[retained++] = (uint32_t)i;
        zcsr_bitmap_set(r->inst_vis, i);
    }
    r->stat_inst_scanned += count;
    r->stat_inst_offscreen += offscreen;
    r->stat_inst_alpha0 += alpha0;

    culled_ratio = (count > 0u)
        ? (float)(offscreen + alpha0) / (float)count : 0.0f;
    if (ZCSR_UNLIKELY(culled_ratio >= ZCSR_CULL_FULL_SCAN_RATIO)) {
        size_t w = 0, k;
        r->stat_inst_cull_path = 1;
        for (k = 0; k < retained; ++k) {
            const zcsr_instance_compact* q = &records[r->inst_sel[k]];
            if (zcsr_gl_compact_offscreen(r, q) ||
                zcsr_gl_compact_alpha8(q) == 0u) continue;
            r->inst_sel[w++] = r->inst_sel[k];
        }
        retained = w;
    } else {
        r->stat_inst_cull_path = 0;
    }
    return retained;
}

/* Shared GL state for the compact entry: make current / target / viewport /
 * program / uniforms. Plain white multiply with packed alpha matches the slim
 * multiply program when it was created at init. */
static bool zcsr_gl_compact_begin_state(zcsr_gl_renderer* r) {
    unsigned int program;
    int uniform_view, uniform_tex;
    if (!r) return false;
    if (!zcsr_gl_make_current(r)) return false;
    if (!zcsr_gl_bind_active_target(r)) return false;
    inst_cache_viewport(r);
    if (r->inst_program_mul) {
        program = r->inst_program_mul;
        uniform_view = r->inst_uniform_view_mul;
        uniform_tex = r->inst_uniform_tex_mul;
    } else {
        program = r->inst_program;
        uniform_view = r->inst_uniform_view;
        uniform_tex = r->inst_uniform_tex;
    }
    inst_cache_program(r, program, uniform_view, uniform_tex, -1);
    glActiveTexture(GL_TEXTURE0);
    r->inst_current_mul = (program == r->inst_program_mul);
    return true;
}

/* Pass 2: gather retained 36 B records by index (same-format copy, no field
 * conversion), upload the slot slice and draw. `stage` is a static no-heap
 * buffer; the 168 B inst_stage array is not carried in this tier. */
static size_t zcsr_gl_compact_upload_draw(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                          const zcsr_instance_compact* records,
                                          size_t retained) {
    static zcsr_instance_compact stage[ZCSR_GL_MAX_INSTANCES];
    const size_t stride = ZCSR_INSTANCE_STRIDE_COMPACT;
    const size_t slot_count = zcsr_inst_slot_count(r);
    size_t done = 0, submitted = 0;
    if (!r || !records || retained == 0 || texture == 0) return 0;
    if (!zcsr_gl_tex_path_a_compatible(r, texture)) return 0;
    while (done < retained) {
        size_t n = retained - done;
        size_t k, offset;
        if (n > (size_t)ZCSR_GL_MAX_INSTANCES) n = (size_t)ZCSR_GL_MAX_INSTANCES;
        for (k = 0; k < n; ++k) {
            stage[k] = records[r->inst_sel[done + k]];
        }
        r->stat_inst_upload_bytes += n * stride;
        {
            const size_t slot_cap = (size_t)ZCSR_GL_MAX_INSTANCES * stride;
            const size_t need = n * stride;
            if (r->inst_skip_upload) {
                /* Diagnostic reuse: keep the slot filled before uploads were
                 * disabled. No slot advance; stale slot-0 prefix, offset zero. */
                r->inst_slot = 0;
                r->inst_frame_off = 0;
                offset = 0;
            } else {
                if (r->inst_frame_off + need > slot_cap) {
                    r->inst_slot = (r->inst_slot + 1u) % (unsigned int)slot_count;
                    r->inst_frame_off = 0;
                    printf("[glrender] compact instance ring wrapped mid-frame: slot=%u chunk=%zu bytes\n",
                           r->inst_slot, need);
                }
                offset = (size_t)r->inst_slot * slot_cap + r->inst_frame_off;
                r->inst_frame_off += need;
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
#if ZCSR_INST_PERSISTENT_RING
        if (!r->inst_persist_ready)
#endif
        {
#ifdef ZCSR_GL_VBO_ORPHAN
            /* Orphaning: discard old storage before partial upload to avoid GPU sync stall.
             * Same condition/order as the generic prepared path. */
            glBufferData(GL_ARRAY_BUFFER,
                         (size_t)ZCSR_GL_INSTANCE_SLOTS * ZCSR_GL_MAX_INSTANCES * stride, NULL,
                         GL_DYNAMIC_DRAW);
#endif
            if (r->inst_orphan) {
                glBufferData(GL_ARRAY_BUFFER,
                             (size_t)ZCSR_GL_INSTANCE_SLOTS * ZCSR_GL_MAX_INSTANCES * stride, NULL,
                             GL_STREAM_DRAW);
            }
        }
        if (!r->inst_skip_upload) {
#if ZCSR_INST_PERSISTENT_RING
            if (r->inst_persist_ready) {
                memcpy((char*)r->inst_persist_map + offset, stage, n * stride);
                ++r->stat_inst_map_writes;
            } else
#endif
            {
                glBufferSubData(GL_ARRAY_BUFFER, offset, n * stride, stage);
                ++r->stat_inst_upload_calls;
            }
        }
        zcsr_gl_inst_bind_slot_attribs(r, offset);
        inst_cache_tex0(r, texture);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)n);
        submitted += n;
        done += n;
    }
    r->stat_inst_touch_bytes += retained * sizeof(uint32_t);
    return submitted;
}

size_t zcsr_gl_draw_instances_compact(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                      const zcsr_instance_compact* records, size_t count) {
    size_t retained, submitted;
    if (!r || !records || count == 0 || texture == 0) return 0;
    retained = zcsr_gl_compact_select(r, records, count);
    if (!zcsr_gl_compact_begin_state(r)) return 0;
    submitted = zcsr_gl_compact_upload_draw(r, texture, records, retained);
    zcsr_gl_instanced_end_state(r);
    return submitted;
}
#endif /* ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT */

void zcsr_gl_set_instance_upload(zcsr_gl_renderer* r, bool enabled) {
    if (r) r->inst_skip_upload = !enabled;
}

void zcsr_gl_set_instance_orphan(zcsr_gl_renderer* r, bool enabled) {
    if (r) r->inst_orphan = enabled;
}

/* Instanced-path diagnostics (rulings 1/2/3/4/9). Read-only getters for probes
 * and tests; counters accumulate since renderer creation and are cleared by
 * the reset call. Getters return 0 on a NULL renderer. */
size_t zcsr_gl_instance_scanned(const zcsr_gl_renderer* r) {
    return r ? r->stat_inst_scanned : 0u;
}

size_t zcsr_gl_instance_touch_bytes(const zcsr_gl_renderer* r) {
    return r ? r->stat_inst_touch_bytes : 0u;
}

size_t zcsr_gl_instance_upload_bytes(const zcsr_gl_renderer* r) {
    return r ? r->stat_inst_upload_bytes : 0u;
}

size_t zcsr_gl_instance_skipped(const zcsr_gl_renderer* r) {
    return r ? r->stat_inst_skipped : 0u;
}

size_t zcsr_gl_instance_dead(const zcsr_gl_renderer* r) {
    return r ? r->stat_inst_dead : 0u;
}

size_t zcsr_gl_instance_offscreen(const zcsr_gl_renderer* r) {
    return r ? r->stat_inst_offscreen : 0u;
}

size_t zcsr_gl_instance_alpha0(const zcsr_gl_renderer* r) {
    return r ? r->stat_inst_alpha0 : 0u;
}

int zcsr_gl_instance_cull_path(const zcsr_gl_renderer* r) {
    return r ? r->stat_inst_cull_path : 0;
}

size_t zcsr_gl_instance_upload_calls(const zcsr_gl_renderer* r) {
    return r ? r->stat_inst_upload_calls : 0u;
}

size_t zcsr_gl_instance_map_writes(const zcsr_gl_renderer* r) {
    return r ? r->stat_inst_map_writes : 0u;
}

bool zcsr_gl_instance_persistent_ready(const zcsr_gl_renderer* r) {
#if ZCSR_INST_PERSISTENT_RING
    return r && r->inst_persist_ready;
#else
    (void)r; return false;
#endif
}

void zcsr_gl_instance_stats_reset(zcsr_gl_renderer* r) {
    if (!r) return;
    r->stat_inst_scanned = 0u;
    r->stat_inst_touch_bytes = 0u;
    r->stat_inst_upload_bytes = 0u;
    r->stat_inst_skipped = 0u;
    r->stat_inst_dead = 0u;
    r->stat_inst_offscreen = 0u;
    r->stat_inst_alpha0 = 0u;
    r->stat_inst_cull_path = 0;
    r->stat_inst_upload_calls = 0u;
    r->stat_inst_map_writes = 0u;
}

void zcsr_gl_set_color_ramp(zcsr_gl_renderer* r, zcsr_gl_texture texture, int rows) {
    zcsr_gl_tex_entry* e;
    if (!r) return;
    if (texture == 0) {
        r->color_ramp_tex = 0;
        r->color_ramp_rows = 0;
        return;
    }
    if (rows < 1 || !zcsr_gl_tex_path_a_compatible(r, texture)) {
        ZCSR_LOG_ERROR("glrender", "set_color_ramp: invalid ramp texture=%u rows=%d",
                       (unsigned)texture, rows);
        return;
    }
    e = zcsr_gl_find_tex(r, texture);
    if (!e || e->h < rows) {
        ZCSR_LOG_ERROR("glrender",
                       "set_color_ramp: ramp must be at least `rows` high (tex=%u h=%d rows=%d)",
                       (unsigned)texture, e ? e->h : 0, rows);
        return;
    }
    r->color_ramp_tex = texture;
    r->color_ramp_rows = rows;
}

/* ===========================================================================================
 * Sort pipeline stage (ruling 7): 4 B key + 4 B index order, pure CPU, no heap.
 * The legacy public wrappers keep the original in-place contract so existing
 * callers and contract tests are unaffected.
 * =========================================================================================== */

static uint32_t zcsr_key_tex(const zcsr_gl_instance* in) { return in->tex; }

/* Map a float to a monotonic uint32 key, then invert for descending (far z first). */
static uint32_t zcsr_key_z_desc(const zcsr_gl_instance* in) {
    uint32_t u;
    uint32_t k;
    memcpy(&u, &in->z, 4);
    k = (u & 0x80000000u) ? ~u : (u | 0x80000000u);
    return ~k;
}

/* Run the sort stage over `items[0..n)` using `key_of`, then apply the resulting
 * stable permutation in place. `temp` is the legacy caller scratch of at least
 * count*sizeof(instance) bytes; the stage uses only n*4 B order, n*4 B radix
 * scratch and n*4 B keys from its front (12 n*4 B total, well inside the
 * 168 B-per-instance contract). The permutation is written back by cycle
 * rotation, so no second 168 B scratch is required. */
static void zcsr_gl_stage_sort(zcsr_gl_instance* items, size_t n, void* temp,
                               uint32_t (*key_of)(const zcsr_gl_instance*)) {
    uint32_t* order;
    uint32_t* scratch;
    uint32_t* keys;
    size_t i;
    if (n < 2u) return;
    order = (uint32_t*)temp;
    scratch = order + n;
    keys = scratch + n;
    for (i = 0; i < n; ++i) keys[i] = key_of(&items[i]);
    zcsr_sort_instance_order(keys, order, scratch, n);
    /* In-place cycle rotation: order[i] is the source slot that belongs at i.
     * Traverse each cycle, shifting values forward and restoring the saved
     * head at the cycle tail; slots are marked fixed as the loop goes. */
    for (i = 0; i < n; ++i) {
        if (order[i] == (uint32_t)i) continue;
        zcsr_gl_instance saved = items[i];
        uint32_t cur = (uint32_t)i;
        while (order[cur] != (uint32_t)i) {
            uint32_t nxt = order[cur];
            items[cur] = items[nxt];
            order[cur] = cur;
            cur = nxt;
        }
        items[cur] = saved;
        order[cur] = cur;
    }
}

size_t zcsr_sort_instances(zcsr_gl_instance* instances, size_t count, void* temp, size_t temp_bytes) {
    size_t runs = 1u, i;
    if (!instances || !temp || count == 0) return 0;
    if (temp_bytes < count * sizeof(zcsr_gl_instance)) return 0;
    zcsr_gl_stage_sort(instances, count, temp, zcsr_key_tex);
    for (i = 1; i < count; ++i) {
        if (instances[i].tex != instances[i - 1].tex) ++runs;
    }
    return count == 0u ? 0u : runs;
}

void zcsr_instance_run(const zcsr_gl_instance* sorted, size_t count, size_t run_index,
                       zcsr_gl_texture* out_texture, size_t* out_start, size_t* out_len) {
    size_t runs = 0, start = 0, i;
    if (!sorted || !out_texture || !out_start || !out_len) return;
    for (i = 0; i <= count; ++i) {
        if (i == count || (i > 0 && sorted[i].tex != sorted[i - 1].tex)) {
            if (runs == run_index) {
                *out_texture = sorted[start].tex;
                *out_start = start;
                *out_len = i - start;
                return;
            }
            ++runs;
            start = i;
        }
    }
    *out_texture = 0;
    *out_start = 0;
    *out_len = 0;
}

#if ZCSR_INSTANCE_LAYOUT != ZCSR_LAYOUT_COMPACT
/* 168 B-record submission conveniences. Generic-tier only: these feed the
 * general entry chain (select / prepared / draw_run_batched), which does not
 * exist in the COMPACT tier. No symbols are produced for them there. */
size_t zcsr_gl_submit_instance_batch(zcsr_gl_renderer* r, zcsr_gl_instance* instances,
                                     size_t count, void* temp, size_t temp_bytes) {
    size_t runs, i, submitted = 0;
    if (!r || !instances || !temp || count == 0) return 0;
    if (temp_bytes < count * sizeof(zcsr_gl_instance)) return 0;
    runs = zcsr_sort_instances(instances, count, temp, temp_bytes);

    /* Shared GL state once for all texture runs (O2: state hoisting). */
    if (!zcsr_gl_instanced_begin_state(r, false)) return 0;
    for (i = 0; i < runs; ++i) {
        zcsr_gl_texture tex;
        size_t start, len;
        zcsr_instance_run(instances, count, i, &tex, &start, &len);
        submitted += zcsr_gl_draw_run_batched(r, tex, instances + start, len);
    }
    zcsr_gl_instanced_end_state(r);
    return submitted;
}

size_t zcsr_gl_submit_sorted_runs(zcsr_gl_renderer* r, const zcsr_gl_sorted_run* runs,
                                  size_t run_count) {
    size_t i, submitted = 0;
    if (!r || !runs || run_count == 0) return 0;
    if (!zcsr_gl_instanced_begin_state(r, false)) return 0;
    for (i = 0; i < run_count; ++i) {
        submitted += zcsr_gl_draw_run_batched(r, runs[i].texture,
                                              runs[i].instances, runs[i].count);
    }
    zcsr_gl_instanced_end_state(r);
    return submitted;
}
#endif /* generic-tier 168 B submit conveniences */

/* ===========================================================================================
 * Full state sort (opaque vs translucent)
 * =========================================================================================== */

static int zcsr_is_translucent(const zcsr_gl_instance* in) {
    return in->has_alpha ||
           in->a < 0.9999f ||
           in->mode == (float)ZCSR_MOD_MIX || in->mode == (float)ZCSR_MOD_ADD;
}

size_t zcsr_sort_state(zcsr_gl_instance* instances, size_t count, void* temp, size_t temp_bytes,
                       size_t* out_opaque) {
    zcsr_gl_instance* scratch = temp;
    size_t i = 0, j = count;
    if (!instances || !temp || count == 0) {
        if (out_opaque) *out_opaque = 0;
        return 0;
    }
    /* Keep the scratch-size multiplication below in range on 32-bit size_t. */
    if (count > ((size_t)-1) / sizeof(zcsr_gl_instance)) {
        if (out_opaque) *out_opaque = 0;
        return 0;
    }
    if (temp_bytes < count * sizeof(zcsr_gl_instance)) {
        if (out_opaque) *out_opaque = 0;
        return 0;
    }
    /* partition: opaque to the front, translucent to the back */
    while (i < j) {
        if (zcsr_is_translucent(&instances[i])) {
            zcsr_gl_instance t;
            --j;
            t = instances[i]; instances[i] = instances[j]; instances[j] = t;
        } else {
            ++i;
        }
    }
    {
        size_t opaque = i;
        /* Tier gating: NONE keeps the opaque/translucent partition only, leaving the
         * draw order as submission order; KEY/FULL keep the existing two-stage sort
         * (opaque by texture ascending, translucent by z far-to-near). */
#if ZCSR_SORT_TIER != ZCSR_SORT_TIER_NONE
        if (opaque > 0)
            zcsr_gl_stage_sort(instances, opaque, scratch, zcsr_key_tex);
        if (count - opaque > 0)
            zcsr_gl_stage_sort(instances + opaque, count - opaque, scratch, zcsr_key_z_desc);
#else
        /* Sort stage compiled out: the legacy scratch view stays unused here. */
        (void)scratch;
#endif
        if (out_opaque) *out_opaque = opaque;
        return opaque;
    }
}

#if ZCSR_INSTANCE_LAYOUT != ZCSR_LAYOUT_COMPACT
size_t zcsr_gl_submit_state(zcsr_gl_renderer* r, zcsr_gl_instance* instances, size_t count,
                            void* temp, size_t temp_bytes) {
    size_t opaque;
    size_t i, submitted = 0;
    if (!r || !instances || !temp || count == 0) return 0;
    if (temp_bytes < count * sizeof(zcsr_gl_instance)) return 0;
    zcsr_sort_state(instances, count, temp, temp_bytes, &opaque);

    /* One shared state setup for both opaque and translucent runs. */
    if (!zcsr_gl_instanced_begin_state(r, false)) return 0;

    /* opaque: draw in texture runs (order irrelevant) */
    i = 0;
    while (i < opaque) {
        size_t start = i;
        uint32_t tex = instances[i].tex;
        while (i < opaque && instances[i].tex == tex) ++i;
        submitted += zcsr_gl_draw_run_batched(r, tex, instances + start, i - start);
    }
    /* translucent: back-to-front, gather only adjacent same-texture (z order preserved) */
    i = opaque;
    while (i < count) {
        size_t start = i;
        uint32_t tex = instances[i].tex;
        while (i < count && instances[i].tex == tex) ++i;
        submitted += zcsr_gl_draw_run_batched(r, tex, instances + start, i - start);
    }
    zcsr_gl_instanced_end_state(r);
    return submitted;
}
#endif /* generic-tier submit_state */
