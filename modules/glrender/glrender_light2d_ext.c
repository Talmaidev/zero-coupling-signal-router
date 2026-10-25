/* glrender_light2d_ext.c — Light2D renderer extension: MRT G-buffer targets, per-frame state
 * save/restore, and the radial (LOW) / deferred (HIGH) light programs with their drawing
 * helpers. Implements the public contract in include/zcsr/gl_render_light2d_ext.h on behalf of
 * modules/light2d (which itself makes no GL calls).
 *
 * Zero heap: every piece of state lives in zcsr_gl_renderer's fixed arrays. All GL entry
 * points make the context current first, mirroring glrender_core.c.
 *
 * Coordinate space is the renderer's standard 2D convention: origin top-left, y down, units
 * are target pixels. The radial vertex shader uses the same projection as the core sprite
 * program (glrender_core.c zcsr_gl_make_program).
 */
#include "glrender_internal.h"
#include "zcsr/gl_render_light2d_ext.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* Must match the u_LightData/u_LightColor array size in the deferred fragment shader and
 * ZCSR_L2D_MAX_LIGHTS in modules/light2d/light2d.c. */
#define ZCSR_L2D_MAX_LIGHT_UNIFORMS 64

/* Interleaved vertex shared by both light programs through fsq_vao: position (2) + uv (2).
 * Radial quads carry absolute pixel positions plus quad-local uv; the deferred fullscreen
 * triangle carries clip-space positions (its vertex shader derives uv from them). */
typedef struct {
    float x, y;
    float u, v;
} zcsr_l2d_vertex;

/* ===========================================================================================
 * Shaders / programs
 * =========================================================================================== */

static unsigned int zcsr_l2d_link_program(const char* label,
                                          const char* vs_src,
                                          const char* fs_src) {
    unsigned int v = zcsr_gl_compile(GL_VERTEX_SHADER, vs_src);
    unsigned int f = zcsr_gl_compile(GL_FRAGMENT_SHADER, fs_src);
    unsigned int p;
    int ok = 0;
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    p = glCreateProgram();
    if (!p) {
        glDeleteShader(v);
        glDeleteShader(f);
        return 0;
    }
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        zcsr_gl_log_object_info(label, p, true);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static unsigned int zcsr_l2d_make_radial_program(void) {
    /* LOW path: one additive radial-falloff quad per light, drawn over the existing scene.
     * The quad is sized 2*radius around the light, so quad-local uv already encodes the
     * falloff radius. */
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_pos;\n"
        "layout(location=1) in vec2 a_uv;\n"
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "void main(){\n"
        "  gl_Position = vec4((a_pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (a_pos.y / u_view.y) * 2.0, 0.0, 1.0);\n"
        "  v_uv = a_uv;\n"
        "}\n";
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "in vec2 v_uv;\n"
        "uniform float u_Radius;\n"
        "uniform float u_Decay;\n"
        "uniform vec4 u_LightColor;\n"
        "out vec4 o_color;\n"
        "void main(){\n"
        "  /* v_uv spans [0,1] across a quad of extent 2*u_Radius centered on the light, so\n"
        "     length(v_uv-0.5)*2*u_Radius is the pixel distance from the center; normalized\n"
        "     by u_Radius it is the quad-local falloff of the light. */\n"
        "  float dist = length(v_uv - vec2(0.5)) * 2.0 * u_Radius;\n"
        "  float atten = pow(clamp(1.0 - dist / u_Radius, 0.0, 1.0), u_Decay);\n"
        "  o_color = u_LightColor * atten;\n"
        "}\n";
    return zcsr_l2d_link_program("light2d radial program link failed", vs, fs);
}

static unsigned int zcsr_l2d_make_deferred_program(void) {
    /* HIGH composite pass: albedo linearized x (emissive + accumulated light +
     * ambient), HDR tonemap x/(x+1), back to gamma for display. */
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_pos;\n"
        "out vec2 v_uv;\n"
        "void main(){\n"
        "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "  v_uv = a_pos * 0.5 + 0.5;\n"
        "}\n";
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "in vec2 v_uv;\n"
        "uniform sampler2D u_Albedo;   /* unit 4 */\n"
        "uniform sampler2D u_Emissive; /* unit 7, HDR RGBA16F */\n"
        "uniform sampler2D u_Light;    /* unit 8, accumulated light buffer */\n"
        "uniform vec3 u_Ambient;\n"
        "out vec4 o_color;\n"
        "void main(){\n"
        "  vec3 albedo = texture(u_Albedo, v_uv).rgb;\n"
        "  vec3 emissive = texture(u_Emissive, v_uv).rgb;\n"
        "  vec3 light = texture(u_Light, v_uv).rgb;\n"
        "  /* gamma space is for display; compute in linear space, tonemap, go back */\n"
        "  vec3 albedo_lin = pow(albedo, vec3(2.2));\n"
        "  vec3 lit = albedo_lin * (emissive + light + u_Ambient);\n"
        "  vec3 mapped = lit / (lit + vec3(1.0));\n"
        "  o_color = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);\n"
        "}\n";
    return zcsr_l2d_link_program("light2d deferred program link failed", vs, fs);
}

static unsigned int zcsr_l2d_make_bloom_program(void) {
    /* Bloom composite variant (reference CameraDefer2DBloomPass): the light and
     * emissive buffers get a 9-tap weighted blur; the blur's HDR brightness
     * gates how much of the blurred color replaces the sharp sample, then the
     * same linearize/tonemap/gamma composite. */
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_pos;\n"
        "out vec2 v_uv;\n"
        "void main(){\n"
        "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "  v_uv = a_pos * 0.5 + 0.5;\n"
        "}\n";
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "in vec2 v_uv;\n"
        "uniform sampler2D u_Albedo;   /* unit 4 */\n"
        "uniform sampler2D u_Emissive; /* unit 7, HDR RGBA16F */\n"
        "uniform sampler2D u_Light;    /* unit 8, accumulated light buffer */\n"
        "uniform vec2 u_LightRes;      /* light buffer size for the tap step */\n"
        "uniform vec3 u_Ambient;\n"
        "out vec4 o_color;\n"
        "vec3 bloom9(sampler2D t, vec2 uv){\n"
        "  vec2 st = vec2(1.5) / max(u_LightRes, vec2(1.0));\n"
        "  vec3 blur =\n"
        "    (texture(t, uv + st * vec2(-1.0,  1.0)).rgb * 0.094742 +\n"
        "     texture(t, uv + st * vec2( 0.0,  1.0)).rgb * 0.118318 +\n"
        "     texture(t, uv + st * vec2( 1.0,  1.0)).rgb * 0.094742 +\n"
        "     texture(t, uv + st * vec2(-1.0,  0.0)).rgb * 0.118318 +\n"
        "     texture(t, uv).rgb * 0.147761 +\n"
        "     texture(t, uv + st * vec2( 1.0,  0.0)).rgb * 0.118318 +\n"
        "     texture(t, uv + st * vec2(-1.0, -1.0)).rgb * 0.094742 +\n"
        "     texture(t, uv + st * vec2( 0.0, -1.0)).rgb * 0.118318 +\n"
        "     texture(t, uv + st * vec2( 1.0, -1.0)).rgb * 0.094742);\n"
        "  vec3 hdr = blur / (blur + vec3(1.0));\n"
        "  float lum = dot(hdr, vec3(0.3, 0.49, 0.11));\n"
        "  vec3 rgb = texture(t, uv).rgb;\n"
        "  return lum * blur + (1.0 - lum) * rgb;\n"
        "}\n"
        "void main(){\n"
        "  vec3 albedo = texture(u_Albedo, v_uv).rgb;\n"
        "  vec3 albedo_lin = pow(albedo, vec3(2.2));\n"
        "  vec3 lit = albedo_lin * (bloom9(u_Emissive, v_uv)\n"
        "                           + bloom9(u_Light, v_uv) + u_Ambient);\n"
        "  vec3 mapped = lit / (lit + vec3(1.0));\n"
        "  o_color = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);\n"
        "}\n";
    return zcsr_l2d_link_program("light2d bloom program link failed", vs, fs);
}

static unsigned int zcsr_l2d_make_shadow_program(void) {
    /* Per-light shadow MASK (JoyECS-style): selected occluder edges extruded
     * away from the light, fragment writes a constant factor, MAX-blended.
     * a_flag.x = 1 marks the extruded copies of the edge endpoints.
     * Point lights extrude radially from u_light; parallel lights extrude
     * along the fixed travel direction u_dir; u_distance bounds the band
     * (finite wall-style drop shadows); u_reverse extrudes toward the light. */
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_pos;\n"
        "layout(location=1) in vec2 a_flag;\n"
        "uniform vec2 u_light;\n"
        "uniform vec2 u_view;\n"
        "uniform vec2 u_dir;\n"       /* parallel: unit travel direction (px space) */
        "uniform float u_distance;\n" /* extrusion length (px) */
        "uniform int u_parallel;\n"
        "uniform int u_reverse;\n"
        "void main(){\n"
        "  vec2 p = a_pos;\n"
        "  if (a_flag.x > 0.5) {\n"
        "    vec2 d;\n"
        "    if (u_parallel != 0) {\n"
        "      d = u_dir;\n"
        "    } else {\n"
        "      vec2 radial = a_pos - u_light;\n"
        "      float len = length(radial);\n"
        "      d = (len < 1e-6) ? vec2(0.0) : radial / len;\n"
        "    }\n"
        "    if (u_reverse != 0) d = -d;\n"
        "    p = a_pos + d * u_distance;\n"
        "  }\n"
        "  gl_Position = vec4((p.x / u_view.x) * 2.0 - 1.0, 1.0 - (p.y / u_view.y) * 2.0, 0.0, 1.0);\n"
        "}\n";
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "uniform float u_factor;\n"
        "out vec4 o_mask;\n"
        "void main(){ o_mask = vec4(u_factor, 0.0, 0.0, 1.0); }\n";
    return zcsr_l2d_link_program("light2d shadow program link failed", vs, fs);
}

static unsigned int zcsr_l2d_make_light_program(void) {
    /* One additive pass per light into the low-res accumulation buffer:
     * pow(1-r/R, decay) * 1/(dist/DIST_SCALE+1) * N.L * (1-shadow) * gain. */
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_pos;\n"
        "layout(location=1) in vec2 a_uv;\n"
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "out vec2 v_pos;\n"
        "void main(){\n"
        "  gl_Position = vec4((a_pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (a_pos.y / u_view.y) * 2.0, 0.0, 1.0);\n"
        "  v_uv = a_uv;\n"
        "  v_pos = a_pos;\n"
        "}\n";
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "in vec2 v_uv;\n"
        "in vec2 v_pos;\n"
        "uniform vec2 u_light;\n"      /* world units */
        "uniform float u_light_z;\n"   /* flame height above the plane, world units */
        "uniform vec3 u_color;\n"
        "uniform float u_radius;\n"    /* world units */
        "uniform float u_decay;\n"
        "uniform float u_gain;\n"
        "uniform float u_dist_scale;\n"/* world units per falloff step */
        "uniform int u_normals_on;\n"
        "uniform int u_use_shadow;\n"
        "uniform int u_parallel;\n"   /* 1: parallel light, dir3 drives N.L */
        "uniform vec3 u_dir3;\n"      /* parallel: unit travel direction (3D) */
        "uniform vec2 u_viewpos;\n"
        "uniform vec2 u_shadowres;\n"  /* mask size, for the bias tap step */
        "uniform int u_ray_shadows;\n" /* 1: per-pixel ray test vs u_Boxes */
        "uniform vec4 u_Boxes[16];\n"  /* 2 per box: (minx,miny,maxx,maxy),(H,0,0,0) */
        "uniform int u_BoxCount;\n"
        "uniform sampler2D u_Position; /* unit 5, world 3D, air = far */\n"
        "uniform sampler2D u_Normal;  /* unit 6, G-buffer */\n"
        "uniform sampler2D u_Shadow;  /* unit 8, this light's mask */\n"
        "out vec4 o_light;\n"
        "/* Geometry Proof v1 slab test: P is shadowed iff the segment L->P\n"
        "     crosses any prism footprint x [0,H]; t-interval intersection\n"
        "     with (eps, 1-eps) so endpoints never self-occlude. */\n"
        "bool zcsr_ray_blocked(vec3 P){\n"
        "  vec3 L = vec3(u_light, u_light_z);\n"
        "  vec3 d = P - L;\n"
        "  for (int i = 0; i < 8; ++i) {\n"
        "    if (i >= u_BoxCount) break;\n"
        "    vec4 b = u_Boxes[i * 2];\n"
        "    float hgt = u_Boxes[i * 2 + 1].x;\n"
        "    float t0 = 1e-4;\n"
        "    float t1 = 1.0 - 1e-4;\n"
        "    /* x slab */\n"
        "    if (abs(d.x) < 1e-6) { if (L.x < b.x || L.x > b.z) continue; }\n"
        "    else {\n"
        "      float ta = (b.x - L.x) / d.x;\n"
        "      float tb = (b.z - L.x) / d.x;\n"
        "      t0 = max(t0, min(ta, tb));\n"
        "      t1 = min(t1, max(ta, tb));\n"
        "    }\n"
        "    /* y slab */\n"
        "    if (abs(d.y) < 1e-6) { if (L.y < b.y || L.y > b.w) continue; }\n"
        "    else {\n"
        "      float ta = (b.y - L.y) / d.y;\n"
        "      float tb = (b.w - L.y) / d.y;\n"
        "      t0 = max(t0, min(ta, tb));\n"
        "      t1 = min(t1, max(ta, tb));\n"
        "    }\n"
        "    /* z slab [0, hgt] */\n"
        "    if (abs(d.z) < 1e-6) { if (L.z < 0.0 || L.z > hgt) continue; }\n"
        "    else {\n"
        "      float ta = (0.0 - L.z) / d.z;\n"
        "      float tb = (hgt - L.z) / d.z;\n"
        "      t0 = max(t0, min(ta, tb));\n"
        "      t1 = min(t1, max(ta, tb));\n"
        "    }\n"
        "    if (t0 < t1) return true;\n"
        "  }\n"
        "  return false;\n"
        "}\n"
        "void main(){\n"
        "  /* scene y-down -> G-buffer v (top row at v=1): flip y when sampling */\n"
        "  vec2 gb_uv = vec2(v_pos.x / u_viewpos.x, 1.0 - v_pos.y / u_viewpos.y);\n"
        "  /* Point lights: all math in WORLD units against the G-buffer's 3D\n"
        "     world position (air clears to a far sentinel -> no light there).\n"
        "     Parallel lights (reference Light2DParallel): NO distance falloff,\n"
        "     a fixed travel direction, fullscreen coverage. */\n"
        "  vec3 pos = texture(u_Position, gb_uv).xyz;\n"
        "  vec3 to_light;\n"
        "  float atten = 1.0;\n"
        "  if (u_parallel != 0) {\n"
        "    to_light = -u_dir3;\n"
        "  } else {\n"
        "    to_light = vec3(u_light, u_light_z) - pos;\n"
        "    float dist = length(to_light);\n"
        "    float r = dist / max(u_radius, 1e-4);\n"
        "    atten = pow(clamp(1.0 - r, 0.0, 1.0), u_decay);\n"
        "    atten *= 1.0 / (dist / max(u_dist_scale, 1e-4) + 1.0);\n"
        "  }\n"
        "  /* JoyEngine semantics: PURE N.L in 3D, no base term. Neutral (0,0)\n"
        "     decodes to the up normal (0,0,1) so camera-facing surfaces are lit\n"
        "     only through the light's elevation; any other value is an in-plane\n"
        "     wall normal (nx,ny,0) that horizontal rays strike full-strength. */\n"
        "  float nl = 1.0;\n"
        "  if (u_normals_on != 0) {\n"
        "    vec2 n_raw = texture(u_Normal, gb_uv).xy * 2.0 - 1.0;\n"
        "    vec3 n = (dot(n_raw, n_raw) < 1e-6) ? vec3(0.0, 0.0, 1.0)\n"
        "                                        : normalize(vec3(n_raw, 0.0));\n"
        "    float tl = length(to_light);\n"
        "    vec3 ldir = (tl < 1e-6) ? vec3(0.0, 0.0, 1.0) : to_light / tl;\n"
        "    nl = max(0.0, dot(n, ldir));\n"
        "  }\n"
        "  /* 5-tap MAX dilate, matching the reference engine's bias sampling. */\n"
        "  float shadow = 0.0;\n"
        "  if (u_use_shadow != 0) {\n"
        "    if (u_ray_shadows != 0 && u_parallel == 0) {\n"
        "      if (zcsr_ray_blocked(pos)) shadow = 1.0;\n"
        "    } else {\n"
        "      vec2 bias = vec2(1.5, 1.5) / max(u_shadowres, vec2(1.0, 1.0));\n"
        "      shadow = max(shadow, texture(u_Shadow, gb_uv).r);\n"
        "      shadow = max(shadow, texture(u_Shadow, gb_uv + bias * vec2(-1.0, -1.0)).r);\n"
        "      shadow = max(shadow, texture(u_Shadow, gb_uv + bias * vec2(-1.0, 1.0)).r);\n"
        "      shadow = max(shadow, texture(u_Shadow, gb_uv + bias * vec2(1.0, -1.0)).r);\n"
        "      shadow = max(shadow, texture(u_Shadow, gb_uv + bias * vec2(1.0, 1.0)).r);\n"
        "    }\n"
        "  }\n"
        "  o_light = vec4(u_color * (u_gain * atten * nl * (1.0 - shadow)), 0.0);\n"
        "}\n";
    return zcsr_l2d_link_program("light2d light program link failed", vs, fs);
}

/* ===========================================================================================
 * Internal helpers
 * =========================================================================================== */

static zcsr_gl_l2d_mrt* zcsr_l2d_slot(zcsr_gl_renderer* r, zcsr_gl_l2d_target target) {
    if (r == NULL || target == ZCSR_GL_L2D_TARGET_INVALID ||
        target > (zcsr_gl_l2d_target)ZCSR_GL_L2D_MAX_TARGETS) {
        return NULL;
    }
    if (!r->l2d_targets[target - 1u].in_use) return NULL;
    return &r->l2d_targets[target - 1u];
}

/* Between-frame target operations (create/resize/destroy) run outside the push/pop window,
 * so they snapshot the bindings they are about to touch and put them back on exit. */
typedef struct {
    int fbo;
    int active_texture;
    unsigned int texture_2d;
} zcsr_l2d_snapshot;

static void zcsr_l2d_snapshot_take(zcsr_l2d_snapshot* snap) {
    GLint value = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &value);
    snap->fbo = value;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &value);
    snap->active_texture = value;
    value = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &value);
    snap->texture_2d = (unsigned int)value;
}

static void zcsr_l2d_snapshot_restore(const zcsr_l2d_snapshot* snap) {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)snap->fbo);
    glActiveTexture((GLenum)snap->active_texture);
    glBindTexture(GL_TEXTURE_2D, snap->texture_2d);
}

static void zcsr_l2d_delete_textures(int count, unsigned int* tex) {
    int i;
    for (i = 0; i < count && i < ZCSR_GL_L2D_MAX_ATTACHMENTS; ++i) {
        if (tex[i] != 0u) glDeleteTextures(1, &tex[i]);
        tex[i] = 0u;
    }
}

/* Creates `count` G-buffer textures at w x h into out_tex[i]. LINEAR + CLAMP_TO_EDGE: no
 * mipmaps, or the default NEAREST_MIPMAP_LINEAR min filter would leave the textures
 * incomplete (they would sample as black). */
static bool zcsr_l2d_create_attachment_textures(int count, const int* iformats, int w, int h,
                                                unsigned int* out_tex) {
    int i;
    for (i = 0; i < count && i < ZCSR_GL_L2D_MAX_ATTACHMENTS; ++i) {
        unsigned int tex = 0;
        GLenum type = (iformats[i] == GL_RGBA16F) ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
        glGenTextures(1, &tex);
        if (tex == 0u) break;
        out_tex[i] = tex;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, iformats[i], w, h, 0, GL_RGBA, type, NULL);
    }
    return i == count;
}

/* The draw entry points reuse fsq_vao/fsq_vbo for two different vertex streams, so they
 * leave the VAO and ARRAY_BUFFER bindings exactly as they found them. */
typedef struct {
    int vao;
    int array_buffer;
} zcsr_l2d_draw_bindings;

static void zcsr_l2d_save_draw_bindings(zcsr_l2d_draw_bindings* saved) {
    GLint value = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &value);
    saved->vao = value;
    value = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &value);
    saved->array_buffer = value;
}

static void zcsr_l2d_restore_draw_bindings(const zcsr_l2d_draw_bindings* saved) {
    glBindVertexArray((GLuint)saved->vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)saved->array_buffer);
}

/* Resolves a uniform name against the currently bound Light2D program (radial,
 * deferred, deferred-bloom or light). Returns false when no Light2D program is bound.
 * *out_location is -1 for inactive uniforms (stripped by the GLSL compiler,
 * e.g. u_Position/u_Normal in the deferred shader); callers treat -1 as a
 * successful no-op so contract-level sets never spuriously fail. Names outside
 * the cached set fall back to a live glGetUniformLocation. */
static bool zcsr_l2d_uniform_location(zcsr_gl_renderer* r, const char* name, int* out_location) {
    GLint current = 0;
    int location = -1;
    if (r == NULL || name == NULL || !zcsr_gl_make_current(r)) return false;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current);
    if (current == 0 ||
        ((unsigned int)current != r->l2d_prog_radial &&
         (unsigned int)current != r->l2d_prog_deferred &&
         (unsigned int)current != r->l2d_prog_bloom &&
         (unsigned int)current != r->l2d_prog_light)) {
        return false;
    }
    if ((unsigned int)current == r->l2d_prog_radial) {
        if (strcmp(name, "u_view") == 0) location = r->l2d_radial_loc_view;
        else if (strcmp(name, "u_Radius") == 0) location = r->l2d_radial_loc_radius;
        else if (strcmp(name, "u_Decay") == 0) location = r->l2d_radial_loc_decay;
        else if (strcmp(name, "u_LightColor") == 0) location = r->l2d_radial_loc_color;
        else location = glGetUniformLocation((GLuint)current, name);
    } else if ((unsigned int)current == r->l2d_prog_bloom) {
        if (strcmp(name, "u_Ambient") == 0) location = r->l2d_bloom_loc_ambient;
        else if (strcmp(name, "u_Albedo") == 0) location = r->l2d_bloom_loc_albedo;
        else if (strcmp(name, "u_Emissive") == 0) location = r->l2d_bloom_loc_emissive;
        else if (strcmp(name, "u_Light") == 0) location = r->l2d_bloom_loc_light;
        else if (strcmp(name, "u_LightRes") == 0) location = r->l2d_bloom_loc_lightres;
        else location = glGetUniformLocation((GLuint)current, name);
    } else {
        if (strcmp(name, "u_Ambient") == 0) location = r->l2d_deferred_loc_ambient;
        else if (strcmp(name, "u_Albedo") == 0) location = r->l2d_deferred_loc_albedo;
        else if (strcmp(name, "u_Emissive") == 0) location = r->l2d_deferred_loc_emissive;
        else if (strcmp(name, "u_Light") == 0) location = r->l2d_deferred_loc_light;
        else if (strcmp(name, "u_Shadow") == 0) location = r->l2d_deferred_loc_shadow;
        else if (strcmp(name, "u_ViewSize") == 0) location = r->l2d_deferred_loc_viewsize;
        else if (strcmp(name, "u_LightCount") == 0) location = r->l2d_deferred_loc_lightcount;
        else if (strcmp(name, "u_LightData") == 0) location = r->l2d_deferred_loc_lightdata;
        else if (strcmp(name, "u_LightColor") == 0) location = r->l2d_deferred_loc_lightcolor;
        else location = glGetUniformLocation((GLuint)current, name);
    }
    *out_location = location;
    return true;
}

/* ===========================================================================================
 * Init / destroy (called from zcsr_gl_create/destroy, context already current)
 * =========================================================================================== */

bool zcsr_gl_l2d_init(zcsr_gl_renderer* r) {
    r->l2d_prog_radial = zcsr_l2d_make_radial_program();
    if (!r->l2d_prog_radial) return false;
    r->l2d_prog_deferred = zcsr_l2d_make_deferred_program();
    if (!r->l2d_prog_deferred) return false;

    r->l2d_radial_loc_view = glGetUniformLocation(r->l2d_prog_radial, "u_view");
    r->l2d_radial_loc_radius = glGetUniformLocation(r->l2d_prog_radial, "u_Radius");
    r->l2d_radial_loc_decay = glGetUniformLocation(r->l2d_prog_radial, "u_Decay");
    r->l2d_radial_loc_color = glGetUniformLocation(r->l2d_prog_radial, "u_LightColor");
    r->l2d_deferred_loc_ambient = glGetUniformLocation(r->l2d_prog_deferred, "u_Ambient");
    r->l2d_deferred_loc_albedo = glGetUniformLocation(r->l2d_prog_deferred, "u_Albedo");
    r->l2d_deferred_loc_position = glGetUniformLocation(r->l2d_prog_deferred, "u_Position");
    r->l2d_deferred_loc_normal = glGetUniformLocation(r->l2d_prog_deferred, "u_Normal");
    r->l2d_deferred_loc_emissive = glGetUniformLocation(r->l2d_prog_deferred, "u_Emissive");
    r->l2d_deferred_loc_shadow = glGetUniformLocation(r->l2d_prog_deferred, "u_Shadow");
    r->l2d_deferred_loc_viewsize = glGetUniformLocation(r->l2d_prog_deferred, "u_ViewSize");
    r->l2d_deferred_loc_lightcount = glGetUniformLocation(r->l2d_prog_deferred, "u_LightCount");
    r->l2d_deferred_loc_lightdata = glGetUniformLocation(r->l2d_prog_deferred, "u_LightData");
    r->l2d_deferred_loc_lightcolor = glGetUniformLocation(r->l2d_prog_deferred, "u_LightColor");
    r->l2d_deferred_loc_light = glGetUniformLocation(r->l2d_prog_deferred, "u_Light");

    r->l2d_prog_bloom = zcsr_l2d_make_bloom_program();
    if (!r->l2d_prog_bloom) return false;
    r->l2d_bloom_loc_ambient = glGetUniformLocation(r->l2d_prog_bloom, "u_Ambient");
    r->l2d_bloom_loc_albedo = glGetUniformLocation(r->l2d_prog_bloom, "u_Albedo");
    r->l2d_bloom_loc_emissive = glGetUniformLocation(r->l2d_prog_bloom, "u_Emissive");
    r->l2d_bloom_loc_light = glGetUniformLocation(r->l2d_prog_bloom, "u_Light");
    r->l2d_bloom_loc_lightres = glGetUniformLocation(r->l2d_prog_bloom, "u_LightRes");

    r->l2d_prog_shadow = zcsr_l2d_make_shadow_program();
    if (!r->l2d_prog_shadow) return false;
    r->l2d_prog_light = zcsr_l2d_make_light_program();
    if (!r->l2d_prog_light) return false;
    r->l2d_shadow_loc_light = glGetUniformLocation(r->l2d_prog_shadow, "u_light");
    r->l2d_shadow_loc_view = glGetUniformLocation(r->l2d_prog_shadow, "u_view");
    r->l2d_shadow_loc_factor = glGetUniformLocation(r->l2d_prog_shadow, "u_factor");
    r->l2d_shadow_loc_dir = glGetUniformLocation(r->l2d_prog_shadow, "u_dir");
    r->l2d_shadow_loc_distance = glGetUniformLocation(r->l2d_prog_shadow, "u_distance");
    r->l2d_shadow_loc_parallel = glGetUniformLocation(r->l2d_prog_shadow, "u_parallel");
    r->l2d_shadow_loc_reverse = glGetUniformLocation(r->l2d_prog_shadow, "u_reverse");
    r->l2d_light_loc_light = glGetUniformLocation(r->l2d_prog_light, "u_light");
    r->l2d_light_loc_view = glGetUniformLocation(r->l2d_prog_light, "u_view");
    r->l2d_light_loc_viewpos = glGetUniformLocation(r->l2d_prog_light, "u_viewpos");
    r->l2d_light_loc_color = glGetUniformLocation(r->l2d_prog_light, "u_color");
    r->l2d_light_loc_radius = glGetUniformLocation(r->l2d_prog_light, "u_radius");
    r->l2d_light_loc_decay = glGetUniformLocation(r->l2d_prog_light, "u_decay");
    r->l2d_light_loc_gain = glGetUniformLocation(r->l2d_prog_light, "u_gain");
    r->l2d_light_loc_normal = glGetUniformLocation(r->l2d_prog_light, "u_Normal");
    r->l2d_light_loc_shadow = glGetUniformLocation(r->l2d_prog_light, "u_Shadow");
    r->l2d_light_loc_position = glGetUniformLocation(r->l2d_prog_light, "u_Position");
    r->l2d_light_loc_px_per_unit = glGetUniformLocation(r->l2d_prog_light, "u_px_per_unit");
    r->l2d_light_loc_use_shadow = glGetUniformLocation(r->l2d_prog_light, "u_use_shadow");
    r->l2d_light_loc_normals_on = glGetUniformLocation(r->l2d_prog_light, "u_normals_on");
    r->l2d_light_loc_dist_scale = glGetUniformLocation(r->l2d_prog_light, "u_dist_scale");
    r->l2d_light_loc_light_z = glGetUniformLocation(r->l2d_prog_light, "u_light_z");
    r->l2d_light_loc_shadowres = glGetUniformLocation(r->l2d_prog_light, "u_shadowres");
    r->l2d_light_loc_parallel = glGetUniformLocation(r->l2d_prog_light, "u_parallel");
    r->l2d_light_loc_dir3 = glGetUniformLocation(r->l2d_prog_light, "u_dir3");

    glGenVertexArrays(1, &r->fsq_vao);
    glGenBuffers(1, &r->fsq_vbo);
    if (!r->fsq_vao || !r->fsq_vbo) return false;

    glBindVertexArray(r->fsq_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->fsq_vbo);
    /* Worst case is the 6-vertex quad; the fullscreen triangle re-uploads 3 vertices per draw. */
    glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(zcsr_l2d_vertex), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(zcsr_l2d_vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(zcsr_l2d_vertex),
                          (void*)offsetof(zcsr_l2d_vertex, u));
    glBindVertexArray(0);

    /* Wire the deferred samplers once at create: G-buffer block on units 4..7, the
     * accumulated light buffer on unit 8 (light2d.c re-states 4..8 with the same
     * values). The light program samples the G-buffer normal (6) and the per-light
     * shadow mask (8, rebound by shadow_end after each light). */
    glUseProgram(r->l2d_prog_deferred);
    if (r->l2d_deferred_loc_albedo >= 0) glUniform1i(r->l2d_deferred_loc_albedo, 4);
    if (r->l2d_deferred_loc_emissive >= 0) glUniform1i(r->l2d_deferred_loc_emissive, 7);
    if (r->l2d_deferred_loc_light >= 0) glUniform1i(r->l2d_deferred_loc_light, 8);
    glUseProgram(0);
    glUseProgram(r->l2d_prog_bloom);
    if (r->l2d_bloom_loc_albedo >= 0) glUniform1i(r->l2d_bloom_loc_albedo, 4);
    if (r->l2d_bloom_loc_emissive >= 0) glUniform1i(r->l2d_bloom_loc_emissive, 7);
    if (r->l2d_bloom_loc_light >= 0) glUniform1i(r->l2d_bloom_loc_light, 8);
    glUseProgram(0);
    glUseProgram(r->l2d_prog_light);
    if (r->l2d_light_loc_normal >= 0) glUniform1i(r->l2d_light_loc_normal, 6);
    if (r->l2d_light_loc_shadow >= 0) glUniform1i(r->l2d_light_loc_shadow, 8);
    if (r->l2d_light_loc_position >= 0) glUniform1i(r->l2d_light_loc_position, 5);
    glUseProgram(0);

    return true;
}

void zcsr_gl_l2d_destroy(zcsr_gl_renderer* r) {
    int i, j;
    if (r == NULL) return;
    /* MRT slots may hold live FBOs/textures even on the partial-init failure path. */
    for (i = 0; i < ZCSR_GL_L2D_MAX_TARGETS; ++i) {
        zcsr_gl_l2d_mrt* slot = &r->l2d_targets[i];
        if (!slot->in_use) continue;
        if (slot->fbo) glDeleteFramebuffers(1, &slot->fbo);
        for (j = 0; j < slot->attachments && j < ZCSR_GL_L2D_MAX_ATTACHMENTS; ++j) {
            if (slot->tex[j]) glDeleteTextures(1, &slot->tex[j]);
        }
        memset(slot, 0, sizeof(*slot));
    }
    if (r->fsq_vbo) glDeleteBuffers(1, &r->fsq_vbo);
    if (r->fsq_vao) glDeleteVertexArrays(1, &r->fsq_vao);
    if (r->l2d_prog_radial) glDeleteProgram(r->l2d_prog_radial);
    if (r->l2d_prog_deferred) glDeleteProgram(r->l2d_prog_deferred);
    if (r->l2d_prog_bloom) glDeleteProgram(r->l2d_prog_bloom);
    if (r->l2d_prog_shadow) glDeleteProgram(r->l2d_prog_shadow);
    if (r->l2d_prog_light) glDeleteProgram(r->l2d_prog_light);
    if (r->l2d_light_fbo) glDeleteFramebuffers(1, &r->l2d_light_fbo);
    if (r->l2d_light_tex) glDeleteTextures(1, &r->l2d_light_tex);
    if (r->l2d_shadow_fbo) glDeleteFramebuffers(1, &r->l2d_shadow_fbo);
    if (r->l2d_shadow_tex) glDeleteTextures(1, &r->l2d_shadow_tex);
}

/* ===========================================================================================
 * State stack
 * =========================================================================================== */

bool zcsr_gl_l2d_state_push(zcsr_gl_renderer* renderer) {
    zcsr_gl_l2d_state* s;
    GLint value = 0;
    int i;
    if (renderer == NULL || !zcsr_gl_make_current(renderer)) return false;
    if (renderer->l2d_state_depth >= ZCSR_GL_L2D_STATE_DEPTH) return false; /* overflow */

    s = &renderer->l2d_state_stack[renderer->l2d_state_depth];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &value);
    s->fbo = value;
    glGetIntegerv(GL_VIEWPORT, s->viewport);
    s->blend = glIsEnabled(GL_BLEND) != 0;
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &s->blend_equation_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &s->blend_equation_alpha);
    glGetIntegerv(GL_BLEND_SRC_RGB, &s->blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &s->blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &s->blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &s->blend_dst_alpha);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &value);
    s->active_texture = value;
    value = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &value);
    s->program = (unsigned int)value;
    for (i = 0; i < ZCSR_GL_L2D_STATE_TEX_UNITS; ++i) {
        glActiveTexture(GL_TEXTURE0 + (GLenum)(4 + i));
        value = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &value);
        s->textures[i] = (unsigned int)value;
    }
    glActiveTexture((GLenum)s->active_texture); /* querying units 4..8 moved it */

    ++renderer->l2d_state_depth;
    return true;
}

bool zcsr_gl_l2d_state_pop(zcsr_gl_renderer* renderer) {
    zcsr_gl_l2d_state* s;
    int i;
    if (renderer == NULL || !zcsr_gl_make_current(renderer)) return false;
    if (renderer->l2d_state_depth <= 0) return false; /* underflow */

    --renderer->l2d_state_depth;
    s = &renderer->l2d_state_stack[renderer->l2d_state_depth];
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)s->fbo);
    glViewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    if (s->blend) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    glBlendFuncSeparate((GLenum)s->blend_src_rgb, (GLenum)s->blend_dst_rgb,
                        (GLenum)s->blend_src_alpha, (GLenum)s->blend_dst_alpha);
    glBlendEquationSeparate((GLenum)s->blend_equation_rgb, (GLenum)s->blend_equation_alpha);
    glUseProgram((GLuint)s->program);
    for (i = 0; i < ZCSR_GL_L2D_STATE_TEX_UNITS; ++i) {
        glActiveTexture(GL_TEXTURE0 + (GLenum)(4 + i));
        glBindTexture(GL_TEXTURE_2D, s->textures[i]);
    }
    glActiveTexture((GLenum)s->active_texture);
    return true;
}

/* ===========================================================================================
 * MRT targets
 * =========================================================================================== */

bool zcsr_gl_l2d_target_create(zcsr_gl_renderer* renderer,
                               int width,
                               int height,
                               const zcsr_gl_l2d_format* formats,
                               size_t attachment_count,
                               zcsr_gl_l2d_target* out_target) {
    static const GLenum draw_buffers[ZCSR_GL_L2D_MAX_ATTACHMENTS] = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3
    };
    zcsr_gl_l2d_mrt* slot;
    zcsr_l2d_snapshot snap;
    int iformats[ZCSR_GL_L2D_MAX_ATTACHMENTS];
    unsigned int tex[ZCSR_GL_L2D_MAX_ATTACHMENTS];
    unsigned int fbo = 0;
    int slot_index = -1;
    int count;
    size_t i;

    if (renderer == NULL || out_target == NULL || formats == NULL) return false;
    if (width <= 0 || height <= 0) return false;
    if (attachment_count == 0u ||
        attachment_count > (size_t)ZCSR_GL_L2D_MAX_ATTACHMENTS) return false;

    for (i = 0u; i < (size_t)ZCSR_GL_L2D_MAX_TARGETS; ++i) {
        if (!renderer->l2d_targets[i].in_use) {
            slot_index = (int)i;
            break;
        }
    }
    if (slot_index < 0) return false; /* no free slot */
    slot = &renderer->l2d_targets[slot_index];

    count = (int)attachment_count;
    for (i = 0u; i < attachment_count; ++i) {
        if (formats[i] == ZCSR_GL_L2D_FORMAT_RGBA8) {
            iformats[i] = GL_RGBA8;
        } else if (formats[i] == ZCSR_GL_L2D_FORMAT_RGBA16F) {
            iformats[i] = GL_RGBA16F;
        } else {
            return false;
        }
        tex[i] = 0u;
    }
    for (; i < (size_t)ZCSR_GL_L2D_MAX_ATTACHMENTS; ++i) {
        iformats[i] = 0;
        tex[i] = 0u;
    }

    if (!zcsr_gl_make_current(renderer)) return false;
    zcsr_l2d_snapshot_take(&snap);

    if (!zcsr_l2d_create_attachment_textures(count, iformats, width, height, tex)) {
        zcsr_l2d_delete_textures(count, tex);
        zcsr_l2d_snapshot_restore(&snap);
        return false;
    }
    glGenFramebuffers(1, &fbo);
    if (fbo == 0u) {
        zcsr_l2d_delete_textures(count, tex);
        zcsr_l2d_snapshot_restore(&snap);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    for (i = 0u; i < attachment_count; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + (GLenum)i,
                               GL_TEXTURE_2D, tex[i], 0);
    }
    glDrawBuffers((GLsizei)count, draw_buffers);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo);
        zcsr_l2d_delete_textures(count, tex);
        zcsr_l2d_snapshot_restore(&snap);
        return false;
    }

    slot->fbo = fbo;
    for (i = 0u; i < (size_t)ZCSR_GL_L2D_MAX_ATTACHMENTS; ++i) slot->tex[i] = tex[i];
    slot->w = width;
    slot->h = height;
    for (i = 0u; i < (size_t)ZCSR_GL_L2D_MAX_ATTACHMENTS; ++i) slot->iformat[i] = iformats[i];
    slot->attachments = count;
    slot->in_use = true;
    *out_target = (zcsr_gl_l2d_target)(slot_index + 1);

    zcsr_l2d_snapshot_restore(&snap);
    return true;
}

bool zcsr_gl_l2d_target_resize(zcsr_gl_renderer* renderer,
                               zcsr_gl_l2d_target target,
                               int width,
                               int height) {
    static const GLenum draw_buffers[ZCSR_GL_L2D_MAX_ATTACHMENTS] = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3
    };
    zcsr_gl_l2d_mrt* slot;
    zcsr_l2d_snapshot snap;
    unsigned int new_tex[ZCSR_GL_L2D_MAX_ATTACHMENTS];
    size_t count;
    size_t i;

    if (renderer == NULL || width <= 0 || height <= 0) return false;
    slot = zcsr_l2d_slot(renderer, target);
    if (slot == NULL) return false;
    if (width == slot->w && height == slot->h) return true;
    if (!zcsr_gl_make_current(renderer)) return false;

    count = (size_t)slot->attachments;
    for (i = 0u; i < (size_t)ZCSR_GL_L2D_MAX_ATTACHMENTS; ++i) new_tex[i] = 0u;
    zcsr_l2d_snapshot_take(&snap);

    /* Transactional: build the new-size textures first, swap them into the FBO, and only
     * commit (delete the old ones, update the slot) after the FBO checks complete. On
     * failure the old attachments are put back and remain usable. */
    if (!zcsr_l2d_create_attachment_textures((int)count, slot->iformat, width, height, new_tex)) {
        zcsr_l2d_delete_textures((int)count, new_tex);
        zcsr_l2d_snapshot_restore(&snap);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, slot->fbo);
    for (i = 0u; i < count; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + (GLenum)i,
                               GL_TEXTURE_2D, new_tex[i], 0);
    }
    glDrawBuffers((GLsizei)count, draw_buffers);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        for (i = 0u; i < count; ++i) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + (GLenum)i,
                                   GL_TEXTURE_2D, slot->tex[i], 0);
        }
        glDrawBuffers((GLsizei)count, draw_buffers);
        zcsr_l2d_delete_textures((int)count, new_tex);
        zcsr_l2d_snapshot_restore(&snap);
        return false;
    }

    zcsr_l2d_delete_textures((int)count, slot->tex);
    for (i = 0u; i < count; ++i) slot->tex[i] = new_tex[i];
    slot->w = width;
    slot->h = height;

    zcsr_l2d_snapshot_restore(&snap);
    return true;
}

void zcsr_gl_l2d_target_destroy(zcsr_gl_renderer* renderer,
                                zcsr_gl_l2d_target target) {
    zcsr_gl_l2d_mrt* slot;
    zcsr_l2d_snapshot snap;
    if (renderer == NULL) return;
    slot = zcsr_l2d_slot(renderer, target);
    if (slot == NULL) return;
    if (zcsr_gl_make_current(renderer)) {
        zcsr_l2d_snapshot_take(&snap);
        glDeleteFramebuffers(1, &slot->fbo);
        zcsr_l2d_delete_textures(slot->attachments, slot->tex);
        zcsr_l2d_snapshot_restore(&snap);
    }
    memset(slot, 0, sizeof(*slot));
}

/* ===========================================================================================
 * Frame binding
 * =========================================================================================== */

bool zcsr_gl_l2d_gbuffer_begin(zcsr_gl_renderer* renderer,
                               zcsr_gl_l2d_target target,
                               int width,
                               int height) {
    static const GLenum draw_buffers[ZCSR_GL_L2D_MAX_ATTACHMENTS] = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3
    };
    static const float clear_zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const float clear_normal[4] = { 0.5f, 0.5f, 0.0f, 1.0f }; /* neutral 2D normal */
    /* Air/background position = far sentinel (half-float max): air is far from
     * every light, so uncovered pixels receive no light (reference engine
     * clears its view-space position channel to zfar for the same effect). */
    static const float clear_far[4] = { 0.0f, 0.0f, 65504.0f, 1.0f };
    zcsr_gl_l2d_mrt* slot;
    int i;

    if (renderer == NULL || width <= 0 || height <= 0) return false;
    slot = zcsr_l2d_slot(renderer, target);
    if (slot == NULL) return false;
    if (!zcsr_gl_make_current(renderer)) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, slot->fbo);
    /* Draw-buffer order is the contract order: Albedo, Position, Normal, Emissive. */
    glDrawBuffers((GLsizei)slot->attachments, draw_buffers);
    glViewport(0, 0, width, height);
    /* Clear by draw-buffer index: Albedo/Emissive zero; Position far; Normal
     * (0.5,0.5,0,1) is the [0,1] encoding of the neutral (up) normal (0,0). */
    for (i = 0; i < slot->attachments; ++i) {
        glClearBufferfv(GL_COLOR, i,
                        (i == 1) ? clear_far : (i == 2) ? clear_normal : clear_zero);
    }
    return true;
}

bool zcsr_gl_l2d_bind_default_target(zcsr_gl_renderer* renderer,
                                     int width,
                                     int height) {
    if (renderer == NULL || width <= 0 || height <= 0) return false;
    if (!zcsr_gl_make_current(renderer)) return false;
    /* Public contract: the HIGH composite goes to the default window target only. */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    return true;
}

bool zcsr_gl_l2d_bind_target_textures(zcsr_gl_renderer* renderer,
                                      zcsr_gl_l2d_target target,
                                      int first_texture_unit) {
    zcsr_gl_l2d_mrt* slot;
    int i;
    if (renderer == NULL || first_texture_unit < 0) return false;
    slot = zcsr_l2d_slot(renderer, target);
    if (slot == NULL) return false;
    if (renderer->white_tex == 0u) return false;
    if (!zcsr_gl_make_current(renderer)) return false;

    for (i = 0; i < slot->attachments; ++i) {
        glActiveTexture(GL_TEXTURE0 + (GLenum)(first_texture_unit + i));
        glBindTexture(GL_TEXTURE_2D, slot->tex[i]);
    }
    /* One unit past the G-buffer block carries the u_Shadow placeholder: the renderer's
     * white texture reads as full visibility (1.0), not a real shadow map. */
    glActiveTexture(GL_TEXTURE0 + (GLenum)(first_texture_unit + slot->attachments));
    glBindTexture(GL_TEXTURE_2D, renderer->white_tex);
    return true;
}

/* ===========================================================================================
 * Programs / uniforms / blend
 * =========================================================================================== */

bool zcsr_gl_l2d_set_program(zcsr_gl_renderer* renderer,
                             zcsr_gl_l2d_program program) {
    unsigned int prog;
    if (renderer == NULL || !zcsr_gl_make_current(renderer)) return false;
    if (program == ZCSR_GL_L2D_PROGRAM_RADIAL) {
        prog = renderer->l2d_prog_radial;
    } else if (program == ZCSR_GL_L2D_PROGRAM_DEFERRED) {
        prog = renderer->l2d_prog_deferred;
    } else if (program == ZCSR_GL_L2D_PROGRAM_DEFERRED_BLOOM) {
        prog = renderer->l2d_prog_bloom;
    } else {
        return false;
    }
    if (prog == 0u) return false; /* zcsr_gl_l2d_init failed at renderer create */
    glUseProgram(prog);
    return true;
}

bool zcsr_gl_l2d_uniform_1f(zcsr_gl_renderer* renderer, const char* name, float x) {
    int location;
    if (!zcsr_l2d_uniform_location(renderer, name, &location)) return false;
    if (location >= 0) glUniform1f(location, x);
    return true;
}

bool zcsr_gl_l2d_uniform_2f(zcsr_gl_renderer* renderer, const char* name, float x, float y) {
    int location;
    if (!zcsr_l2d_uniform_location(renderer, name, &location)) return false;
    if (location >= 0) glUniform2f(location, x, y);
    return true;
}

bool zcsr_gl_l2d_uniform_3f(zcsr_gl_renderer* renderer,
                            const char* name, float x, float y, float z) {
    int location;
    if (!zcsr_l2d_uniform_location(renderer, name, &location)) return false;
    if (location >= 0) glUniform3f(location, x, y, z);
    return true;
}

bool zcsr_gl_l2d_uniform_4f(zcsr_gl_renderer* renderer,
                            const char* name, float x, float y, float z, float w) {
    int location;
    if (!zcsr_l2d_uniform_location(renderer, name, &location)) return false;
    if (location >= 0) glUniform4f(location, x, y, z, w);
    return true;
}

bool zcsr_gl_l2d_uniform_1i(zcsr_gl_renderer* renderer, const char* name, int value) {
    int location;
    if (!zcsr_l2d_uniform_location(renderer, name, &location)) return false;
    if (location >= 0) glUniform1i(location, value);
    return true;
}

bool zcsr_gl_l2d_uniform_vec4_array(zcsr_gl_renderer* renderer,
                                    const char* name,
                                    const float* values,
                                    size_t vec4_count) {
    int location;
    if (vec4_count > (size_t)ZCSR_L2D_MAX_LIGHT_UNIFORMS) return false;
    if (vec4_count > 0u && values == NULL) return false;
    if (!zcsr_l2d_uniform_location(renderer, name, &location)) return false;
    if (location >= 0 && vec4_count > 0u) {
        glUniform4fv(location, (GLsizei)vec4_count, values);
    }
    return true;
}

bool zcsr_gl_l2d_set_blend_mode(zcsr_gl_renderer* renderer,
                                zcsr_gl_l2d_blend_mode mode) {
    if (renderer == NULL || !zcsr_gl_make_current(renderer)) return false;
    if (mode == ZCSR_GL_L2D_BLEND_OPAQUE) {
        glDisable(GL_BLEND);
    } else if (mode == ZCSR_GL_L2D_BLEND_ADDITIVE) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glBlendEquation(GL_FUNC_ADD); /* true addition even if the app left another equation */
    } else {
        return false;
    }
    return true;
}

/* ===========================================================================================
 * Drawing
 * =========================================================================================== */

bool zcsr_gl_l2d_draw_fullscreen_quad(zcsr_gl_renderer* renderer) {
    static const zcsr_l2d_vertex triangle[3] = {
        { -1.0f, -1.0f, 0.0f, 0.0f },
        {  3.0f, -1.0f, 2.0f, 0.0f },
        { -1.0f,  3.0f, 0.0f, 2.0f }
    };
    zcsr_l2d_draw_bindings saved;
    GLint viewport[4];
    GLint current = 0;

    if (renderer == NULL || !zcsr_gl_make_current(renderer)) return false;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current);
    if ((unsigned int)current != renderer->l2d_prog_deferred &&
        (unsigned int)current != renderer->l2d_prog_bloom) return false;
    if (renderer->fsq_vao == 0u || renderer->fsq_vbo == 0u) return false;

    /* The deferred shader reconstructs fragment positions in light space, so it needs the
     * current target size; the viewport is the authoritative source. */
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] > 0 && viewport[3] > 0 && renderer->l2d_deferred_loc_viewsize >= 0) {
        glUniform2f(renderer->l2d_deferred_loc_viewsize,
                    (float)viewport[2], (float)viewport[3]);
    }

    zcsr_l2d_save_draw_bindings(&saved);
    glBindVertexArray(renderer->fsq_vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->fsq_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    zcsr_l2d_restore_draw_bindings(&saved);
    return true;
}

bool zcsr_gl_l2d_draw_quad(zcsr_gl_renderer* renderer,
                           float x, float y, float width, float height) {
    zcsr_l2d_draw_bindings saved;
    GLint viewport[4];
    GLint current = 0;
    float x1, y1;
    zcsr_l2d_vertex verts[6];

    if (renderer == NULL || !zcsr_gl_make_current(renderer)) return false;
    if (!isfinite(x) || !isfinite(y) || !isfinite(width) || !isfinite(height)) return false;
    if (width <= 0.0f || height <= 0.0f) return false;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current);
    if ((unsigned int)current != renderer->l2d_prog_radial) return false;
    if (renderer->fsq_vao == 0u || renderer->fsq_vbo == 0u) return false;

    /* The radial program projects absolute pixel positions through u_view exactly like the
     * core sprite program; the current viewport is the authoritative target size. */
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return false;
    if (renderer->l2d_radial_loc_view >= 0) {
        glUniform2f(renderer->l2d_radial_loc_view, (float)viewport[2], (float)viewport[3]);
    }

    x1 = x + width;
    y1 = y + height;
    verts[0].x = x;  verts[0].y = y;  verts[0].u = 0.0f; verts[0].v = 0.0f;
    verts[1].x = x1; verts[1].y = y;  verts[1].u = 1.0f; verts[1].v = 0.0f;
    verts[2].x = x;  verts[2].y = y1; verts[2].u = 0.0f; verts[2].v = 1.0f;
    verts[3].x = x;  verts[3].y = y1; verts[3].u = 0.0f; verts[3].v = 1.0f;
    verts[4].x = x1; verts[4].y = y;  verts[4].u = 1.0f; verts[4].v = 0.0f;
    verts[5].x = x1; verts[5].y = y1; verts[5].u = 1.0f; verts[5].v = 1.0f;

    zcsr_l2d_save_draw_bindings(&saved);
    glBindVertexArray(renderer->fsq_vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->fsq_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    zcsr_l2d_restore_draw_bindings(&saved);
    return true;
}

/* ===========================================================================================
 * Light2d v2 passes: per-light shadow mask + low-res light accumulation
 * =========================================================================================== */

/* Ensures the R8 shadow-mask FBO exists at w x h (recreated on size change). The mask
 * is LINEAR filtered so the half-resolution sampling softens edges slightly. */
static bool zcsr_l2d_ensure_shadow_target(zcsr_gl_renderer* r, int w, int h) {
    unsigned int tex = 0;
    if (r->l2d_shadow_fbo != 0 && r->l2d_shadow_tex != 0 &&
        r->l2d_shadow_w == w && r->l2d_shadow_h == h) {
        return true;
    }
    /* unbind from unit 8 before touching the storage (feedback-loop guard) */
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenTextures(1, &tex);
    if (tex == 0u) return false;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    if (r->l2d_shadow_fbo == 0u) glGenFramebuffers(1, &r->l2d_shadow_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->l2d_shadow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &tex);
        return false;
    }
    if (r->l2d_shadow_tex != 0u) glDeleteTextures(1, &r->l2d_shadow_tex);
    r->l2d_shadow_tex = tex;
    r->l2d_shadow_w = w;
    r->l2d_shadow_h = h;
    return true;
}

/* Ensures the RGBA16F light accumulation FBO exists at w x h (recreated on change). */
static bool zcsr_l2d_ensure_light_target(zcsr_gl_renderer* r, int w, int h) {
    unsigned int tex = 0;
    if (r->l2d_light_fbo != 0 && r->l2d_light_tex != 0 &&
        r->l2d_light_w == w && r->l2d_light_h == h) {
        return true;
    }
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenTextures(1, &tex);
    if (tex == 0u) return false;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
    if (r->l2d_light_fbo == 0u) glGenFramebuffers(1, &r->l2d_light_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->l2d_light_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &tex);
        return false;
    }
    if (r->l2d_light_tex != 0u) glDeleteTextures(1, &r->l2d_light_tex);
    r->l2d_light_tex = tex;
    r->l2d_light_w = w;
    r->l2d_light_h = h;
    return true;
}

bool zcsr_gl_l2d_shadow_begin(zcsr_gl_renderer* renderer,
                              int mask_w, int mask_h,
                              int view_w, int view_h,
                              float src_x, float src_y,
                              float dir_x, float dir_y,
                              float distance_px,
                              int parallel) {
    if (renderer == NULL || mask_w <= 0 || mask_h <= 0 ||
        view_w <= 0 || view_h <= 0) {
        return false;
    }
    if (!isfinite(src_x) || !isfinite(src_y)) return false;
    if (!isfinite(dir_x) || !isfinite(dir_y) || !isfinite(distance_px)) return false;
    if (distance_px <= 0.0f) return false;
    if (!zcsr_gl_make_current(renderer)) return false;
    if (!zcsr_l2d_ensure_shadow_target(renderer, mask_w, mask_h)) return false;

    /* the mask is about to be rendered into: drop any binding of it on unit 8 */
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, renderer->l2d_shadow_fbo);
    glViewport(0, 0, mask_w, mask_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(renderer->l2d_prog_shadow);
    if (renderer->l2d_shadow_loc_light >= 0) {
        glUniform2f(renderer->l2d_shadow_loc_light, src_x, src_y);
    }
    if (renderer->l2d_shadow_loc_view >= 0) {
        glUniform2f(renderer->l2d_shadow_loc_view, (float)view_w, (float)view_h);
    }
    if (renderer->l2d_shadow_loc_dir >= 0) {
        glUniform2f(renderer->l2d_shadow_loc_dir, dir_x, dir_y);
    }
    if (renderer->l2d_shadow_loc_distance >= 0) {
        glUniform1f(renderer->l2d_shadow_loc_distance, distance_px);
    }
    if (renderer->l2d_shadow_loc_parallel >= 0) {
        glUniform1i(renderer->l2d_shadow_loc_parallel, parallel ? 1 : 0);
    }
    /* MAX blend: overlapping occluders never over-darken the mask */
    glEnable(GL_BLEND);
    glBlendEquation(GL_MAX);
    return true;
}

bool zcsr_gl_l2d_shadow_edge(zcsr_gl_renderer* renderer,
                             float ax, float ay, float bx, float by,
                             float factor, int reverse, float distance_px) {
    zcsr_l2d_draw_bindings saved;
    zcsr_l2d_vertex verts[6]; /* second attribute channel carries the extrude flag */
    GLint current = 0;

    if (renderer == NULL || !zcsr_gl_make_current(renderer)) return false;
    if (!isfinite(ax) || !isfinite(ay) || !isfinite(bx) || !isfinite(by)) return false;
    if (!isfinite(factor) || factor < 0.0f) return false;
    if (!isfinite(distance_px) || distance_px <= 0.0f) return false;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current);
    if ((unsigned int)current != renderer->l2d_prog_shadow) return false;
    if (renderer->fsq_vao == 0u || renderer->fsq_vbo == 0u) return false;

    if (renderer->l2d_shadow_loc_factor >= 0) {
        glUniform1f(renderer->l2d_shadow_loc_factor, factor);
    }
    if (renderer->l2d_shadow_loc_reverse >= 0) {
        glUniform1i(renderer->l2d_shadow_loc_reverse, reverse ? 1 : 0);
    }
    if (renderer->l2d_shadow_loc_distance >= 0) {
        glUniform1f(renderer->l2d_shadow_loc_distance, distance_px);
    }

    verts[0].x = ax; verts[0].y = ay; verts[0].u = 0.0f; verts[0].v = 0.0f;
    verts[1].x = bx; verts[1].y = by; verts[1].u = 0.0f; verts[1].v = 0.0f;
    verts[2].x = bx; verts[2].y = by; verts[2].u = 1.0f; verts[2].v = 0.0f;
    verts[3].x = ax; verts[3].y = ay; verts[3].u = 0.0f; verts[3].v = 0.0f;
    verts[4].x = bx; verts[4].y = by; verts[4].u = 1.0f; verts[4].v = 0.0f;
    verts[5].x = ax; verts[5].y = ay; verts[5].u = 1.0f; verts[5].v = 0.0f;

    zcsr_l2d_save_draw_bindings(&saved);
    glBindVertexArray(renderer->fsq_vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->fsq_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    zcsr_l2d_restore_draw_bindings(&saved);
    return true;
}

bool zcsr_gl_l2d_shadow_fill(zcsr_gl_renderer* renderer,
                             const float* xy_px, size_t count, float factor) {
    enum { ZCSR_L2D_SHADOW_FILL_MAX_VERTS = 16 };
    zcsr_l2d_draw_bindings saved;
    zcsr_l2d_vertex verts[ZCSR_L2D_SHADOW_FILL_MAX_VERTS];
    GLint current = 0;
    size_t i;

    if (renderer == NULL || xy_px == NULL) return false;
    if (count < 3u || count > (size_t)ZCSR_L2D_SHADOW_FILL_MAX_VERTS) return false;
    if (!zcsr_gl_make_current(renderer)) return false;
    if (!isfinite(factor) || factor < 0.0f) return false;
    for (i = 0u; i < count * 2u; ++i) {
        if (!isfinite(xy_px[i])) return false;
    }
    glGetIntegerv(GL_CURRENT_PROGRAM, &current);
    if ((unsigned int)current != renderer->l2d_prog_shadow) return false;
    if (renderer->fsq_vao == 0u || renderer->fsq_vbo == 0u) return false;

    if (renderer->l2d_shadow_loc_factor >= 0) {
        glUniform1f(renderer->l2d_shadow_loc_factor, factor);
    }
    if (renderer->l2d_shadow_loc_reverse >= 0) {
        glUniform1i(renderer->l2d_shadow_loc_reverse, 0);
    }
    /* flag 0 on every vertex: no extrusion, the polygon itself is the mask */
    for (i = 0u; i < count; ++i) {
        verts[i].x = xy_px[i * 2u];
        verts[i].y = xy_px[i * 2u + 1u];
        verts[i].u = 0.0f;
        verts[i].v = 0.0f;
    }

    zcsr_l2d_save_draw_bindings(&saved);
    glBindVertexArray(renderer->fsq_vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->fsq_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(verts[0])), verts,
                 GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, (GLint)count);
    zcsr_l2d_restore_draw_bindings(&saved);
    return true;
}

bool zcsr_gl_l2d_shadow_end(zcsr_gl_renderer* renderer) {
    if (renderer == NULL || !zcsr_gl_make_current(renderer)) return false;
    /* publish this light's mask on unit 8 for the following light_emit */
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, renderer->l2d_shadow_tex);
    glBlendEquation(GL_FUNC_ADD);
    return true;
}

bool zcsr_gl_l2d_lightbuf_begin(zcsr_gl_renderer* renderer,
                                int buf_w, int buf_h,
                                int view_w, int view_h,
                                float gain, float dist_scale,
                                float px_per_unit,
                                int normals_on) {
    if (renderer == NULL || buf_w <= 0 || buf_h <= 0 ||
        view_w <= 0 || view_h <= 0) {
        return false;
    }
    if (!isfinite(gain) || !isfinite(dist_scale) || gain < 0.0f || dist_scale <= 0.0f) {
        return false;
    }
    if (!isfinite(px_per_unit) || px_per_unit <= 0.0f) return false;
    if (!zcsr_gl_make_current(renderer)) return false;
    if (!zcsr_l2d_ensure_light_target(renderer, buf_w, buf_h)) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, renderer->l2d_light_fbo);
    glViewport(0, 0, buf_w, buf_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(renderer->l2d_prog_light);
    if (renderer->l2d_light_loc_view >= 0) {
        glUniform2f(renderer->l2d_light_loc_view, (float)view_w, (float)view_h);
    }
    if (renderer->l2d_light_loc_viewpos >= 0) {
        glUniform2f(renderer->l2d_light_loc_viewpos, (float)view_w, (float)view_h);
    }
    if (renderer->l2d_light_loc_gain >= 0) {
        glUniform1f(renderer->l2d_light_loc_gain, gain);
    }
    if (renderer->l2d_light_loc_dist_scale >= 0) {
        glUniform1f(renderer->l2d_light_loc_dist_scale, dist_scale);
    }
    if (renderer->l2d_light_loc_px_per_unit >= 0) {
        glUniform1f(renderer->l2d_light_loc_px_per_unit, px_per_unit);
    }
    if (renderer->l2d_light_loc_normals_on >= 0) {
        glUniform1i(renderer->l2d_light_loc_normals_on, normals_on ? 1 : 0);
    }
    /* light_emit sizes its quads in pixels from world-unit radii via this. */
    renderer->l2d_px_per_unit = px_per_unit;
    renderer->l2d_view_w = view_w;
    renderer->l2d_view_h = view_h;
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBlendEquation(GL_FUNC_ADD);
    return true;
}

bool zcsr_gl_l2d_light_emit(zcsr_gl_renderer* renderer,
                            const zcsr_gl_l2d_light_draw* light) {
    zcsr_l2d_draw_bindings saved;
    zcsr_l2d_vertex verts[6];
    float x, y, x1, y1;

    if (renderer == NULL || light == NULL || !zcsr_gl_make_current(renderer)) return false;
    if (!isfinite(light->quad_x) || !isfinite(light->quad_y) ||
        !isfinite(light->wx) || !isfinite(light->wy) || !isfinite(light->height) ||
        !isfinite(light->radius) || !isfinite(light->decay) ||
        !isfinite(light->cr) || !isfinite(light->cg) || !isfinite(light->cb)) return false;
    if (!isfinite(light->dir_x) || !isfinite(light->dir_y) || !isfinite(light->dir_z)) return false;
    if (!light->parallel && light->radius <= 0.0f) return false;
    if (renderer->l2d_prog_light == 0u) return false;
    if (renderer->l2d_light_fbo == 0u || renderer->l2d_light_tex == 0u) return false;
    if (renderer->fsq_vao == 0u || renderer->fsq_vbo == 0u) return false;

    /* the shadow pass rebound the mask FBO and its program meanwhile; accumulate here */
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->l2d_light_fbo);
    glViewport(0, 0, renderer->l2d_light_w, renderer->l2d_light_h);
    glUseProgram(renderer->l2d_prog_light);

    /* point: u_light/height/radius are WORLD units (shader math runs in world
     * space); the quad geometry is pixel-space: caller-projected center +
     * zoom-sized. parallel: fixed direction, NO falloff, fullscreen quad. */
    if (renderer->l2d_light_loc_light >= 0) {
        glUniform2f(renderer->l2d_light_loc_light, light->wx, light->wy);
    }
    if (renderer->l2d_light_loc_light_z >= 0) {
        glUniform1f(renderer->l2d_light_loc_light_z, light->height);
    }
    if (renderer->l2d_light_loc_shadowres >= 0) {
        glUniform2f(renderer->l2d_light_loc_shadowres,
                   (float)(renderer->l2d_shadow_w > 0 ? renderer->l2d_shadow_w : 1),
                   (float)(renderer->l2d_shadow_h > 0 ? renderer->l2d_shadow_h : 1));
    }
    if (renderer->l2d_light_loc_color >= 0) {
        glUniform3f(renderer->l2d_light_loc_color, light->cr, light->cg, light->cb);
    }
    if (renderer->l2d_light_loc_radius >= 0) {
        glUniform1f(renderer->l2d_light_loc_radius, light->radius);
    }
    if (renderer->l2d_light_loc_decay >= 0) {
        glUniform1f(renderer->l2d_light_loc_decay, light->decay);
    }
    if (renderer->l2d_light_loc_use_shadow >= 0) {
        glUniform1i(renderer->l2d_light_loc_use_shadow, light->use_shadow ? 1 : 0);
    }
    if (renderer->l2d_light_loc_parallel >= 0) {
        glUniform1i(renderer->l2d_light_loc_parallel, light->parallel ? 1 : 0);
    }
    if (renderer->l2d_light_loc_dir3 >= 0) {
        glUniform3f(renderer->l2d_light_loc_dir3, light->dir_x, light->dir_y, light->dir_z);
    }

    if (light->parallel) {
        const float vw = (float)(renderer->l2d_view_w > 0 ? renderer->l2d_view_w : 1);
        const float vh = (float)(renderer->l2d_view_h > 0 ? renderer->l2d_view_h : 1);
        x = 0.0f;
        y = 0.0f;
        x1 = vw;
        y1 = vh;
    } else {
        const float half_px = light->radius * renderer->l2d_px_per_unit;
        x = light->quad_x - half_px;
        y = light->quad_y - half_px;
        x1 = light->quad_x + half_px;
        y1 = light->quad_y + half_px;
    }
    verts[0].x = x;  verts[0].y = y;  verts[0].u = 0.0f; verts[0].v = 0.0f;
    verts[1].x = x1; verts[1].y = y;  verts[1].u = 1.0f; verts[1].v = 0.0f;
    verts[2].x = x;  verts[2].y = y1; verts[2].u = 0.0f; verts[2].v = 1.0f;
    verts[3].x = x;  verts[3].y = y1; verts[3].u = 0.0f; verts[3].v = 1.0f;
    verts[4].x = x1; verts[4].y = y;  verts[4].u = 1.0f; verts[4].v = 0.0f;
    verts[5].x = x1; verts[5].y = y1; verts[5].u = 1.0f; verts[5].v = 1.0f;

    zcsr_l2d_save_draw_bindings(&saved);
    glBindVertexArray(renderer->fsq_vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->fsq_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    zcsr_l2d_restore_draw_bindings(&saved);
    return true;
}

bool zcsr_gl_l2d_lightbuf_end(zcsr_gl_renderer* renderer) {
    if (renderer == NULL || !zcsr_gl_make_current(renderer)) return false;
    /* the composite step owns the next target/blend switch */
    return true;
}

bool zcsr_gl_l2d_bind_light_texture(zcsr_gl_renderer* renderer,
                                    int texture_unit) {
    if (renderer == NULL || texture_unit < 0) return false;
    if (renderer->l2d_light_tex == 0u) return false;
    if (!zcsr_gl_make_current(renderer)) return false;
    glActiveTexture(GL_TEXTURE0 + (GLenum)texture_unit);
    glBindTexture(GL_TEXTURE_2D, renderer->l2d_light_tex);
    return true;
}
