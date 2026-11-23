/* glrender_instanced.c — instanced rendering program, init/destroy, draw, radix sort, state sort.
 * Uses core's texture table (zcsr_gl_find_tex) and platform hooks (make_current/swap/update_size).
 */
#include "glrender_internal.h"
#include "zcsr/diagnostics.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* glad exposes GL_TEXTURE0 but not the consecutive unit-1 token on every
 * platform; keep the same local fallback used by glrender_l2d_geo.c. */
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif

/* ===========================================================================================
 * Instanced shader / program
 * =========================================================================================== */

static unsigned int zcsr_gl_make_inst_program(void) {
    /* Unit-quad corner (per-vertex) + per-instance attributes (divisor 1). The vertex shader
     * expands the corner into the rotated quad, so the CPU only uploads instance data. */
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
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
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
        "out vec4 o_color;\n"
        "vec3 display_to_linear(vec3 c){\n"
        "  c = clamp(c, 0.0, 1.0);\n"
        "  vec3 lo = c / 12.92;\n"
        "  vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));\n"
        "  return mix(lo, hi, step(vec3(0.04045), c));\n"
        "}\n"
        "vec3 rgb_to_hsv(vec3 c){\n"
        "  c = clamp(c, 0.0, 1.0);\n"
        "  vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);\n"
        "  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));\n"
        "  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));\n"
        "  float d = q.x - min(q.w, q.y);\n"
        "  float e = 1.0e-10;\n"
        "  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);\n"
        "}\n"
        "vec3 hsv_to_rgb(vec3 c){\n"
        "  vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);\n"
        "  return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);\n"
        "}\n"
        "vec3 rgb_to_oklab(vec3 c){\n"
        "  c = clamp(c, 0.0, 1.0);\n"
        "  float l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;\n"
        "  float m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;\n"
        "  float s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;\n"
        "  vec3 lms = sign(vec3(l, m, s)) * pow(abs(vec3(l, m, s)), vec3(1.0 / 3.0));\n"
        "  return vec3(0.2104542553 * lms.x + 0.7936177850 * lms.y - 0.0040720468 * lms.z,\n"
        "              1.9779984951 * lms.x - 2.4285922050 * lms.y + 0.4505937099 * lms.z,\n"
        "              0.0259040371 * lms.x + 0.7827717662 * lms.y - 0.8086757660 * lms.z);\n"
        "}\n"
        "vec3 oklab_to_rgb(vec3 lab){\n"
        "  float l_ = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;\n"
        "  float m_ = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;\n"
        "  float s_ = lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z;\n"
        "  vec3 lms = vec3(l_, m_, s_);\n"
        "  lms = lms * lms * lms;\n"
        "  return clamp(vec3(\n"
        "      4.0767416621 * lms.x - 3.3077115913 * lms.y + 0.2309699292 * lms.z,\n"
        "     -1.2684380046 * lms.x + 2.6097574011 * lms.y - 0.3413193965 * lms.z,\n"
        "     -0.0041960863 * lms.x - 0.7034186147 * lms.y + 1.7076147010 * lms.z), 0.0, 1.0);\n"
        "}\n"
        "vec3 apply_recolor(vec3 rgb, vec4 h, vec4 r, vec4 sv){\n"
        "  float family_w = 1.0;\n"
        "  if (h.w > 0.0) {\n"
        "    vec3 hsv = rgb_to_hsv(rgb);\n"
        "    if (hsv.y > 0.02) {\n"
        "      float hue = hsv.x * 360.0;\n"
        "      float dist = abs(mod(hue - h.x + 540.0, 360.0) - 180.0);\n"
        "      float edge = max(h.z, 0.5);\n"
        "      float w = 1.0 - smoothstep(edge, edge * 1.5, dist);\n"
        "      family_w = w;\n"
        "      /* Compress the source-family hue spread into a tight target\n"
        "       * family; a fixed angular shift would send green-yellow pixels\n"
        "       * to magenta when green is mapped to red. */\n"
        "      float rel = (hue - h.x + 540.0);\n"
        "      rel = mod(rel, 360.0) - 180.0;\n"
        "      float spread = min(h.z * 0.15, 10.0);\n"
        "      float nh = mod(h.y + (rel / edge) * spread + 360.0, 360.0) / 360.0;\n"
        "      hsv.x = mix(hsv.x, nh, clamp(w * h.w, 0.0, 1.0));\n"
        "      rgb = hsv_to_rgb(hsv);\n"
        "    } else {\n"
        "      family_w = 0.0;\n"
        "    }\n"
        "  }\n"
        "  if (r.y > 0.0) {\n"
        "    float luma = dot(clamp(rgb, 0.0, 1.0), vec3(0.2126, 0.7152, 0.0722));\n"
        "    float rows = max(float(textureSize(u_ramp, 0).y), 1.0);\n"
        "    vec3 ramp = texture(u_ramp, vec2(clamp(luma, 0.0, 1.0), (r.x + 0.5) / rows)).rgb;\n"
        "    float ramp_a = clamp(r.y * family_w, 0.0, 1.0);\n"
        "    if (r.z > 0.5) {\n"
        "      /* Material-masked chroma gradient: replace a/b in OKLab, keep the\n"
        "       * source L (and optionally blend toward ramp L). A full RGB ramp\n"
        "       * overwrites every layer's luminance and is exactly what flattened\n"
        "       * the authored shadows/highlights. */\n"
        "      vec3 src_lab = rgb_to_oklab(rgb);\n"
        "      vec3 ramp_lab = rgb_to_oklab(ramp);\n"
        "      vec3 dst_lab = vec3(mix(src_lab.x, ramp_lab.x, clamp(r.w, 0.0, 1.0)),\n"
        "                          ramp_lab.yz);\n"
        "      rgb = oklab_to_rgb(mix(src_lab, dst_lab, ramp_a));\n"
        "    } else {\n"
        "      rgb = mix(rgb, clamp(ramp, 0.0, 1.0), ramp_a);\n"
        "    }\n"
        "  }\n"
        "  /* Chroma/value conditioning runs after the ramp: a stylized gradient\n"
        "   * can desaturate a low-chroma source, so the target S/V must be the\n"
        "   * final authored state, not an intermediate one. */\n"
        "  if (sv.x > 0.0 || sv.y > 0.0 || abs(sv.z) > 0.0 || abs(sv.w) > 0.0) {\n"
        "    vec3 hsv = rgb_to_hsv(rgb);\n"
        "    if (hsv.y > 0.02) {\n"
        "      float sv_w = (h.w > 0.0) ? family_w : 1.0;\n"
        "      float s = hsv.y;\n"
        "      float s_target = s;\n"
        "      if (sv.x > 0.0) s_target = s_target + (1.0 - s_target) * clamp(sv.x, 0.0, 1.0);\n"
        "      if (sv.y > 0.0) s_target = max(s_target, clamp(sv.y, 0.0, 1.0));\n"
        "      s = mix(s, s_target, clamp(sv_w, 0.0, 1.0));\n"
        "      float v = hsv.z;\n"
        "      float v_target = v;\n"
        "      if (abs(sv.w) > 0.0) v_target = (v_target - 0.5) * (1.0 + sv.w) + 0.5;\n"
        "      if (abs(sv.z) > 0.0) v_target = v_target * (1.0 + sv.z);\n"
        "      v = mix(v, v_target, clamp(sv_w, 0.0, 1.0));\n"
        "      hsv.y = clamp(s, 0.0, 1.0);\n"
        "      hsv.z = clamp(v, 0.0, 1.0);\n"
        "      rgb = hsv_to_rgb(hsv);\n"
        "    }\n"
        "  }\n"
        "  return rgb;\n"
        "}\n"
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

/* ===========================================================================================
 * Instanced init / destroy
 * =========================================================================================== */

bool zcsr_gl_instanced_init(zcsr_gl_renderer* r) {
    static const float corners[12] = {
        -1.f, -1.f,   1.f, -1.f,   1.f, 1.f,
        -1.f, -1.f,   1.f, 1.f,   -1.f, 1.f
    };
    static const unsigned short indices[6] = { 0, 1, 2, 0, 2, 3 };
    const size_t stride = sizeof(zcsr_gl_instance);

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
    glBufferData(GL_ARRAY_BUFFER,
                 (size_t)ZCSR_GL_INSTANCE_SLOTS * ZCSR_GL_MAX_INSTANCES * stride,
                 NULL, GL_DYNAMIC_DRAW);
    r->inst_slot = ZCSR_GL_INSTANCE_SLOTS - 1;

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, cx));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, w));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, u0));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, r));
    glVertexAttribDivisor(4, 1);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, rotation));
    glVertexAttribDivisor(5, 1);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, amount));
    glVertexAttribDivisor(6, 1);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, mode));
    glVertexAttribDivisor(7, 1);
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void*)offsetof(zcsr_gl_instance, recolor));
    glVertexAttribDivisor(8, 1);
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void*)(offsetof(zcsr_gl_instance, recolor) + 4 * sizeof(float)));
    glVertexAttribDivisor(9, 1);
    glEnableVertexAttribArray(10);
    glVertexAttribPointer(10, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void*)offsetof(zcsr_gl_instance, recolor.glow_r));
    glVertexAttribDivisor(10, 1);
    glEnableVertexAttribArray(11);
    glVertexAttribPointer(11, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void*)offsetof(zcsr_gl_instance, recolor.sat_boost));
    glVertexAttribDivisor(11, 1);
    glEnableVertexAttribArray(12);
    glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void*)offsetof(zcsr_gl_instance, recolor.emiss_r));
    glVertexAttribDivisor(12, 1);
    glEnableVertexAttribArray(13);
    glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void*)offsetof(zcsr_gl_instance, recolor.rim_r));
    glVertexAttribDivisor(13, 1);

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
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, cx));
        glVertexAttribDivisor(1, 1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, w));
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, u0));
        glVertexAttribDivisor(3, 1);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, r));
        glVertexAttribDivisor(4, 1);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, rotation));
        glVertexAttribDivisor(5, 1);
    }

    glBindVertexArray(0);
    return r->inst_vao != 0 && r->inst_vbo != 0;
}

void zcsr_gl_instanced_destroy(zcsr_gl_renderer* r) {
    if (r->inst_vbo) glDeleteBuffers(1, &r->inst_vbo);
    if (r->inst_ebo) glDeleteBuffers(1, &r->inst_ebo);
    if (r->inst_corner_vbo) glDeleteBuffers(1, &r->inst_corner_vbo);
    if (r->inst_vao) glDeleteVertexArrays(1, &r->inst_vao);
    if (r->inst_vao_mul) glDeleteVertexArrays(1, &r->inst_vao_mul);
    if (r->inst_program) glDeleteProgram(r->inst_program);
    if (r->inst_program_mul) glDeleteProgram(r->inst_program_mul);
}

/* ===========================================================================================
 * Public API: draw_instances
 * =========================================================================================== */

/* Internal: per-run draw with GL state already set up (context current, framebuffer/viewport/
 * program/uniforms/VAO bound). Only uploads VBO, binds texture, issues draw. No find_tex. */
/* Point the current slot's VAO at its slice of the ring buffer. Called once per
 * uploaded batch; the slot advances so an in-flight draw is never overwritten. */
static void zcsr_gl_inst_bind_slot_attribs(zcsr_gl_renderer* r) {
    const size_t stride = sizeof(zcsr_gl_instance);
    const size_t base = (size_t)r->inst_slot * ZCSR_GL_MAX_INSTANCES * stride;
    glBindVertexArray(r->inst_current_mul && r->inst_vao_mul ? r->inst_vao_mul : r->inst_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void*)(base + offsetof(zcsr_gl_instance, cx)));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void*)(base + offsetof(zcsr_gl_instance, w)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void*)(base + offsetof(zcsr_gl_instance, u0)));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void*)(base + offsetof(zcsr_gl_instance, r)));
    glVertexAttribDivisor(4, 1);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                          (void*)(base + offsetof(zcsr_gl_instance, rotation)));
    glVertexAttribDivisor(5, 1);
    if (!r->inst_current_mul) {
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                              (void*)(base + offsetof(zcsr_gl_instance, amount)));
        glVertexAttribDivisor(6, 1);
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                              (void*)(base + offsetof(zcsr_gl_instance, mode)));
        glVertexAttribDivisor(7, 1);
        glEnableVertexAttribArray(8);
        glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                              (void*)(base + offsetof(zcsr_gl_instance, recolor)));
        glVertexAttribDivisor(8, 1);
        glEnableVertexAttribArray(9);
        glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                              (void*)(base + offsetof(zcsr_gl_instance, recolor) + 4 * sizeof(float)));
        glVertexAttribDivisor(9, 1);
        glEnableVertexAttribArray(10);
        glVertexAttribPointer(10, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                              (void*)(base + offsetof(zcsr_gl_instance, recolor.glow_r)));
        glVertexAttribDivisor(10, 1);
        glEnableVertexAttribArray(11);
        glVertexAttribPointer(11, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                              (void*)(base + offsetof(zcsr_gl_instance, recolor.sat_boost)));
        glVertexAttribDivisor(11, 1);
        glEnableVertexAttribArray(12);
        glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                              (void*)(base + offsetof(zcsr_gl_instance, recolor.emiss_r)));
        glVertexAttribDivisor(12, 1);
        glEnableVertexAttribArray(13);
        glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                              (void*)(base + offsetof(zcsr_gl_instance, recolor.rim_r)));
        glVertexAttribDivisor(13, 1);
    }
}

static void zcsr_gl_draw_instances_prepared(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                             const zcsr_gl_instance* instances, size_t count) {
    const size_t stride = sizeof(zcsr_gl_instance);
    size_t offset;
    if (r->inst_skip_upload) {
        r->inst_slot = 0;           /* reuse the slot filled before uploads were disabled */
    } else {
        r->inst_slot = (r->inst_slot + 1u) % ZCSR_GL_INSTANCE_SLOTS;
    }
    offset = (size_t)r->inst_slot * ZCSR_GL_MAX_INSTANCES * stride;
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
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
    if (!r->inst_skip_upload) {
        glBufferSubData(GL_ARRAY_BUFFER, offset, count * stride, instances);
    }
    zcsr_gl_inst_bind_slot_attribs(r);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)count);
}

/* Set up the shared instanced GL state once for a sequence of texture runs. */
static bool zcsr_gl_instanced_begin_state(zcsr_gl_renderer* r, bool multiply_only) {
    unsigned int program;
    int uniform_view, uniform_tex;
    if (!r) return false;
    if (!zcsr_gl_make_current(r)) return false;
    if (!zcsr_gl_bind_active_target(r)) return false;
    glViewport(0, 0, r->width, r->height);
    if (multiply_only && r->inst_program_mul) {
        program = r->inst_program_mul;
        uniform_view = r->inst_uniform_view_mul;
        uniform_tex = r->inst_uniform_tex_mul;
    } else {
        program = r->inst_program;
        uniform_view = r->inst_uniform_view;
        uniform_tex = r->inst_uniform_tex;
    }
    glUseProgram(program);
    glUniform2f(uniform_view, (float)r->width, (float)r->height);
    glUniform1i(uniform_tex, 0);
    if (r->inst_uniform_ramp >= 0) glUniform1i(r->inst_uniform_ramp, 1);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, r->color_ramp_tex);
    glActiveTexture(GL_TEXTURE0);
    r->inst_current_mul = (program == r->inst_program_mul);
    return true;
}

static void zcsr_gl_instanced_end_state(void) {
    glBindVertexArray(0);
}

static bool zcsr_gl_instance_valid(const zcsr_gl_instance* in) {
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

/* Draw one texture run, splitting it into upload-cap-sized chunks. Shared state
 * (framebuffer/program/VAO) is already set by the caller. Returns submitted count. */
static size_t zcsr_gl_draw_run_batched(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                       const zcsr_gl_instance* instances, size_t count) {
    size_t remaining, i;
    const zcsr_gl_instance* batch;
    if (!r || !instances || count == 0 || texture == 0) return 0;
    if (!zcsr_gl_tex_path_a_compatible(r, texture)) return 0;
    for (i = 0; i < count; ++i) {
        if (!zcsr_gl_instance_valid(&instances[i])) return 0;
        if (instances[i].recolor.ramp_amount > 0.0f && r->color_ramp_tex == 0) {
            ZCSR_LOG_ERROR("glrender", "draw_instances: ramp_row requested with no ramp atlas bound");
            return 0;
        }
    }
    remaining = count;
    batch = instances;
    while (remaining > 0u) {
        size_t n = remaining > (size_t)ZCSR_GL_MAX_INSTANCES
                 ? (size_t)ZCSR_GL_MAX_INSTANCES : remaining;
        zcsr_gl_draw_instances_prepared(r, texture, batch, n);
        batch += n;
        remaining -= n;
    }
    return count;
}

size_t zcsr_gl_draw_instances(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                              const zcsr_gl_instance* instances, size_t count) {
    size_t submitted;
    size_t i;
    bool multiply_only = true;
    if (!r || !instances || count == 0 || texture == 0) return 0;
    for (i = 0; i < count; ++i) {
        if (instances[i].mode != (float)ZCSR_MOD_MULTIPLY ||
            instances[i].recolor.hue_amount > 0.0f ||
            instances[i].recolor.ramp_amount > 0.0f ||
            instances[i].recolor.glow_amount > 0.0f ||
            instances[i].recolor.sat_boost > 0.0f ||
            instances[i].recolor.sat_floor > 0.0f ||
            fabsf(instances[i].recolor.value_gain) > 0.0f ||
            fabsf(instances[i].recolor.value_contrast) > 0.0f ||
            instances[i].recolor.emiss_amount > 0.0f ||
            instances[i].recolor.rim_amount > 0.0f) {
            multiply_only = false;
            break;
        }
    }
    if (!zcsr_gl_instanced_begin_state(r, multiply_only)) return 0;
    submitted = zcsr_gl_draw_run_batched(r, texture, instances, count);
    zcsr_gl_instanced_end_state();
    return submitted;
}

void zcsr_gl_set_instance_upload(zcsr_gl_renderer* r, bool enabled) {
    if (r) r->inst_skip_upload = !enabled;
}

void zcsr_gl_set_instance_orphan(zcsr_gl_renderer* r, bool enabled) {
    if (r) r->inst_orphan = enabled;
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
 * Radix sort (LSD, 4 passes of 8 bits, stable, no heap)
 * =========================================================================================== */

typedef uint32_t (*zcsr_instance_key_fn)(const zcsr_gl_instance*);

static void zcsr_gl_radix(zcsr_gl_instance* items, size_t count,
                          zcsr_gl_instance* scratch, zcsr_instance_key_fn key_of) {
    int shift, b;
    zcsr_gl_instance* src = items;
    zcsr_gl_instance* dst = scratch;
    if (count < 2) return;
    for (shift = 0; shift < 32; shift += 8) {
        uint32_t hist[256];
        size_t i;
        for (b = 0; b < 256; ++b) hist[b] = 0;
        for (i = 0; i < count; ++i) ++hist[(key_of(&src[i]) >> shift) & 0xFFu];
        uint32_t total = 0;
        for (b = 0; b < 256; ++b) {
            uint32_t c = hist[b];
            hist[b] = total;
            total += c;
        }
        for (i = 0; i < count; ++i) {
            uint32_t key = key_of(&src[i]);
            dst[hist[(key >> shift) & 0xFFu]++] = src[i];
        }
        { zcsr_gl_instance* tmp = src; src = dst; dst = tmp; }
    }
    /* 4 even passes end with src==items. Guard for odd pass counts. */
    if (src != items) memcpy(items, src, count * sizeof(zcsr_gl_instance));
}

static uint32_t zcsr_key_tex(const zcsr_gl_instance* in) { return in->tex; }

/* Map a float to a monotonic uint32 key, then invert for descending (far z first). */
static uint32_t zcsr_key_z_desc(const zcsr_gl_instance* in) {
    uint32_t u;
    memcpy(&u, &in->z, 4);
    uint32_t k = (u & 0x80000000u) ? ~u : (u | 0x80000000u);
    return ~k;
}

static void zcsr_gl_radix_by_tex(zcsr_gl_instance* items, size_t count,
                                 zcsr_gl_instance* scratch) {
    zcsr_gl_radix(items, count, scratch, zcsr_key_tex);
}

size_t zcsr_sort_instances(zcsr_gl_instance* instances, size_t count, void* temp, size_t temp_bytes) {
    size_t runs = 0, i;
    if (!instances || !temp || count == 0) return 0;
    if (temp_bytes < count * sizeof(zcsr_gl_instance)) return 0;
    zcsr_gl_radix_by_tex(instances, count, temp);
    for (i = 0; i < count; ++i) {
        if (i == 0 || instances[i].tex != instances[i - 1].tex) ++runs;
    }
    return runs;
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
    zcsr_gl_instanced_end_state();
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
    zcsr_gl_instanced_end_state();
    return submitted;
}

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
        if (opaque > 0) zcsr_gl_radix(instances, opaque, scratch, zcsr_key_tex);
        if (count - opaque > 0)
            zcsr_gl_radix(instances + opaque, count - opaque, scratch, zcsr_key_z_desc);
        if (out_opaque) *out_opaque = opaque;
        return opaque;
    }
}

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
    zcsr_gl_instanced_end_state();
    return submitted;
}
