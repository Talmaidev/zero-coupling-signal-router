/* glrender_core.c — shader/program/VAO-VBO-FBO/white-tex/texture CRUD/clear/set_target.
 * Platform-independent GL logic. Context must be current and glad loaded before core_init.
 */
#include "glrender_internal.h"
#include "zcsr/diagnostics.h"

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ===========================================================================================
 * Shader / program helpers
 * =========================================================================================== */

void zcsr_gl_log_object_info(
    const char* label, unsigned int object, bool is_program) {
    char log[2048];
    GLsizei length = 0;
    if (is_program) {
        glGetProgramInfoLog(object, (GLsizei)sizeof(log), &length, log);
    } else {
        glGetShaderInfoLog(object, (GLsizei)sizeof(log), &length, log);
    }
    if (length < 0) length = 0;
    if (length >= (GLsizei)sizeof(log)) length = (GLsizei)sizeof(log) - 1;
    log[length] = '\0';
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "zcsr-gl", "%s: %s",
        label, log[0] ? log : "(empty info log)");
#else
    ZCSR_LOG_ERROR("glrender", "%s: %s",
                   label, log[0] ? log : "(empty info log)");
#endif
}

unsigned int zcsr_gl_compile(unsigned int type, const char* src) {
    unsigned int shader = glCreateShader(type);
    int ok = 0;
    if (!shader) return 0;
    glShaderSource(shader, 1, &src, 0);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        zcsr_gl_log_object_info(
            type == GL_VERTEX_SHADER
                ? "vertex shader compile failed"
                : type == GL_FRAGMENT_SHADER
                    ? "fragment shader compile failed"
                    : "shader compile failed",
            shader, false);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static unsigned int zcsr_gl_make_program(void) {
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_pos;\n"
        "layout(location=1) in vec2 a_uv;\n"
        "layout(location=2) in vec4 a_color;\n"
        "layout(location=3) in vec2 a_mod;\n"
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_color;\n"
        "out vec2 v_mod;\n"
        "void main(){\n"
        "  vec2 p = vec2((a_pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (a_pos.y / u_view.y) * 2.0);\n"
        "  gl_Position = vec4(p, 0.0, 1.0);\n"
        "  v_uv = a_uv; v_color = a_color; v_mod = a_mod;\n"
        "}\n";
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "in vec2 v_uv;\n"
        "in vec4 v_color;\n"
        "in vec2 v_mod;\n"
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
        "  vec3 tint_rgb = display_to_linear(v_color.rgb);\n"
        "  vec4 tint = vec4(tint_rgb, v_color.a);\n"
        "  float amount = clamp(v_mod.y, 0.0, 1.0);\n"
        "  if (v_mod.x < 0.5) o_color = texel * tint;\n"
        "  else if (v_mod.x < 1.5) o_color = mix(texel, tint, amount);\n"
        "  else o_color = clamp(texel + tint * amount, 0.0, 1.0);\n"
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
        zcsr_gl_log_object_info("sprite program link failed", p, true);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static unsigned int zcsr_gl_create_white_texture(void) {
    unsigned char px[4] = { 255, 255, 255, 255 };
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return tex;
}

/* 8x8 bitmap font data (ASCII 32-126), defined in glrender_text.c */
extern const uint8_t zcsr_font8x8[95][8];

static unsigned int zcsr_gl_create_font_texture(void) {
    /* 128x64 RGBA atlas: 16 cols x 8 rows of 8x8 glyph slots.
     * LSB = leftmost pixel (no mirror). Texture row 0 = font last row (no upside-down).
     * Foreground = opaque white, modulated by vertex color; background = transparent. */
    static unsigned char pixels[128 * 64 * 4];
    unsigned int tex = 0;
    int ch, gy, gx;
    memset(pixels, 0, sizeof(pixels));
    for (ch = 0; ch < 95; ++ch) {
        int col = ch % 16;
        int row = ch / 16;
        for (gy = 0; gy < 8; ++gy) {
            uint8_t bits = zcsr_font8x8[ch][gy]; /* no flip: OpenGL texture bottom = screen top */
            for (gx = 0; gx < 8; ++gx) {
                if (bits & (1u << gx)) { /* LSB = left */
                    int px = col * 8 + gx;
                    int py = row * 8 + gy;
                    int off = (py * 128 + px) * 4;
                    pixels[off + 0] = 255;
                    pixels[off + 1] = 255;
                    pixels[off + 2] = 255;
                    pixels[off + 3] = 255;
                }
            }
        }
    }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return tex;
}

/* ===========================================================================================
 * Texture table (shared with sprite/instanced/text via internal.h)
 * O5: hash lookup (256-slot open addressing) replaces O(128) linear scan.
 * =========================================================================================== */

#define TEX_INDEX_SIZE 512
#define TEX_INDEX_MASK (TEX_INDEX_SIZE - 1)

static int zcsr_tex_index_lookup(const zcsr_gl_renderer* r, uint32_t key) {
    size_t h = (size_t)(key & TEX_INDEX_MASK);
    for (int i = 0; i < TEX_INDEX_SIZE; ++i) {
        size_t slot = (h + (size_t)i) & TEX_INDEX_MASK;
        if (r->tex_index_key[slot] == 0) return -1;
        if (r->tex_index_key[slot] == key) return (int)r->tex_index_val[slot];
    }
    return -1;
}

static void zcsr_tex_index_insert(zcsr_gl_renderer* r, uint32_t key, uint8_t value) {
    size_t h = (size_t)(key & TEX_INDEX_MASK);
    for (int i = 0; i < TEX_INDEX_SIZE; ++i) {
        size_t slot = (h + (size_t)i) & TEX_INDEX_MASK;
        if (r->tex_index_key[slot] == 0 || r->tex_index_key[slot] == key) {
            r->tex_index_key[slot] = key;
            r->tex_index_val[slot] = value;
            return;
        }
    }
}

static void zcsr_tex_index_remove(zcsr_gl_renderer* r, uint32_t key) {
    size_t h = (size_t)(key & TEX_INDEX_MASK);
    for (int i = 0; i < TEX_INDEX_SIZE; ++i) {
        size_t slot = (h + (size_t)i) & TEX_INDEX_MASK;
        if (r->tex_index_key[slot] == key) {
            r->tex_index_key[slot] = 0;
            /* rehash subsequent entries in the same probe chain */
            for (int j = i + 1; j < TEX_INDEX_SIZE; ++j) {
                size_t next = (h + (size_t)j) & TEX_INDEX_MASK;
                if (r->tex_index_key[next] == 0) break;
                uint32_t k = r->tex_index_key[next];
                uint8_t v = r->tex_index_val[next];
                r->tex_index_key[next] = 0;
                zcsr_tex_index_insert(r, k, v);
            }
            return;
        }
        if (r->tex_index_key[slot] == 0) return;
    }
}

zcsr_gl_tex_entry* zcsr_gl_find_tex(zcsr_gl_renderer* r, zcsr_gl_texture t) {
    int idx;
    if (!r || t == 0) return 0;
    idx = zcsr_tex_index_lookup(r, (uint32_t)t);
    if (idx < 0) return 0;
    return &r->textures[idx];
}

zcsr_gl_tex_entry* zcsr_gl_free_tex(zcsr_gl_renderer* r) {
    size_t i;
    if (!r) return 0;
    for (i = 0; i < ZCSR_GL_MAX_TEXTURES; ++i) {
        if (!r->textures[i].in_use) return &r->textures[i];
    }
    return 0;
}

bool zcsr_gl_tex_path_a_compatible(const zcsr_gl_renderer* r, zcsr_gl_texture t) {
    zcsr_gl_tex_entry* e;
    if (!r || t == 0) return false;
    if (t == r->white_tex || t == r->font_tex) return true;
    e = zcsr_gl_find_tex((zcsr_gl_renderer*)r, t);
    return e != NULL && e->color_space == ZCSR_COLOR_SPACE_DISPLAY;
}

/* ===========================================================================================
 * Core init / destroy (does NOT touch instanced resources — caller coordinates lifecycle)
 * =========================================================================================== */

bool zcsr_gl_core_init(zcsr_gl_renderer* r) {
    r->program = zcsr_gl_make_program();
    if (!r->program) return false;
    r->uniform_view = glGetUniformLocation(r->program, "u_view");
    r->uniform_tex = glGetUniformLocation(r->program, "u_tex");
    if (r->uniform_view < 0 || r->uniform_tex < 0) {
        ZCSR_LOG_ERROR("glrender", "sprite program missing required uniform(s): view=%d tex=%d",
                       r->uniform_view, r->uniform_tex);
        glDeleteProgram(r->program);
        r->program = 0;
        return false;
    }

    glGenVertexArrays(1, &r->vao);
    glGenBuffers(1, &r->vbo);
    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(r->vertices), 0, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(zcsr_gl_vertex), (void*)offsetof(zcsr_gl_vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(zcsr_gl_vertex), (void*)offsetof(zcsr_gl_vertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(zcsr_gl_vertex), (void*)offsetof(zcsr_gl_vertex, r));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(zcsr_gl_vertex), (void*)offsetof(zcsr_gl_vertex, mode));

    glGenFramebuffers(1, &r->fbo);
    r->white_tex = zcsr_gl_create_white_texture();
    r->font_tex = zcsr_gl_create_font_texture();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return r->vao != 0 && r->vbo != 0 && r->fbo != 0 && r->white_tex != 0 && r->font_tex != 0;
}

void zcsr_gl_core_destroy(zcsr_gl_renderer* r) {
    size_t i;
    for (i = 0; i < ZCSR_GL_MAX_TEXTURES; ++i) {
        if (r->textures[i].in_use && r->textures[i].id) glDeleteTextures(1, &r->textures[i].id);
    }
    if (r->white_tex) glDeleteTextures(1, &r->white_tex);
    if (r->font_tex) glDeleteTextures(1, &r->font_tex);
    if (r->fbo) glDeleteFramebuffers(1, &r->fbo);
    if (r->vbo) glDeleteBuffers(1, &r->vbo);
    if (r->vao) glDeleteVertexArrays(1, &r->vao);
    if (r->program) glDeleteProgram(r->program);
    /* lazily-created light2d geometry submission resources (no-op if unused) */
    zcsr_gl_l2d_geo_cleanup(r);
}

/* ===========================================================================================
 * Unified linear frame target
 * =========================================================================================== */

static bool zcsr_gl_frame_resize(zcsr_gl_renderer* r, int w, int h) {
    unsigned int tex = 0;
    unsigned int fbo = 0;
    if (r == NULL || w <= 0 || h <= 0) return false;
    if (r->frame_fbo != 0 && r->frame_tex != 0 &&
        r->frame_w == w && r->frame_h == h) {
        return true;
    }
    if (r->frame_fbo) glDeleteFramebuffers(1, &r->frame_fbo);
    if (r->frame_tex) glDeleteTextures(1, &r->frame_tex);
    r->frame_fbo = 0;
    r->frame_tex = 0;
    r->frame_w = 0;
    r->frame_h = 0;

    glGenTextures(1, &tex);
    if (tex == 0u) return false;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0,
                 GL_RGBA, GL_HALF_FLOAT, NULL);
    glGenFramebuffers(1, &fbo);
    if (fbo == 0u) {
        glDeleteTextures(1, &tex);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    {
        static const GLenum draw = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &draw);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        return false;
    }
    r->frame_fbo = fbo;
    r->frame_tex = tex;
    r->frame_w = w;
    r->frame_h = h;
    {
        /* Clear the freshly allocated frame without leaking two pieces of
         * global state into the caller's draw: scissor enable and clear color.
         * A resize can happen lazily inside frame_bind while a scissor clip is
         * active, so leaving scissor disabled would silently widen later draws. */
        GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        GLfloat prev_clear[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_clear);
        zcsr_gl_set_framebuffer_srgb(false);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(prev_clear[0], prev_clear[1], prev_clear[2], prev_clear[3]);
        if (scissor) glEnable(GL_SCISSOR_TEST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool zcsr_gl_frame_init(zcsr_gl_renderer* r) {
    unsigned int v, f, p;
    int ok = 0;
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "out vec2 v_uv;\n"
        "void main(){\n"
        "  vec2 p;\n"
        "  if (gl_VertexID == 1) p = vec2(3.0, -1.0);\n"
        "  else if (gl_VertexID == 2) p = vec2(-1.0, 3.0);\n"
        "  else p = vec2(-1.0, -1.0);\n"
        "  v_uv = p * 0.5 + 0.5;\n"
        "  gl_Position = vec4(p, 0.0, 1.0);\n"
        "}\n";
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "in vec2 v_uv;\n"
        "uniform sampler2D u_Frame;\n"
        "uniform sampler2D u_ColorLut;\n"
        "out vec4 o_color;\n"
        "vec3 display_encode(vec3 x){\n"
        "  x = max(x, vec3(0.0));\n"
        "  float m = max(max(x.r, x.g), x.b);\n"
        "  if (m > 1.0) x = x / (1.0 + (m - 1.0));\n"
        "  x = clamp(x, 0.0, 1.0);\n"
        "  return vec3(texture(u_ColorLut, vec2(x.r, 0.5)).r,\n"
        "              texture(u_ColorLut, vec2(x.g, 0.5)).g,\n"
        "              texture(u_ColorLut, vec2(x.b, 0.5)).b);\n"
        "}\n"
        "void main(){\n"
        "  vec3 linear = max(texture(u_Frame, v_uv).rgb, vec3(0.0));\n"
        "  o_color = vec4(display_encode(linear), 1.0);\n"
        "}\n";
    if (r == NULL) return false;
    if (r->frame_prog != 0u && r->frame_vao != 0u) return true;
    v = zcsr_gl_compile(GL_VERTEX_SHADER, vs);
    f = zcsr_gl_compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return false;
    }
    p = glCreateProgram();
    if (!p) {
        glDeleteShader(v);
        glDeleteShader(f);
        return false;
    }
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        zcsr_gl_log_object_info("frame encode program link failed", p, true);
        glDeleteProgram(p);
        return false;
    }
    r->frame_prog = p;
    r->frame_loc_tex = glGetUniformLocation(p, "u_Frame");
    r->frame_loc_lut = glGetUniformLocation(p, "u_ColorLut");
    glGenVertexArrays(1, &r->frame_vao);
    if (r->frame_vao == 0u ||
        r->frame_loc_tex < 0 || r->frame_loc_lut < 0 ||
        !zcsr_gl_frame_resize(r, 1, 1)) {
        zcsr_gl_frame_destroy(r);
        return false;
    }
    return true;
}

void zcsr_gl_frame_destroy(zcsr_gl_renderer* r) {
    if (r == NULL) return;
    if (r->frame_fbo) glDeleteFramebuffers(1, &r->frame_fbo);
    if (r->frame_tex) glDeleteTextures(1, &r->frame_tex);
    if (r->frame_vao) glDeleteVertexArrays(1, &r->frame_vao);
    if (r->frame_prog) glDeleteProgram(r->frame_prog);
    r->frame_fbo = 0;
    r->frame_tex = 0;
    r->frame_vao = 0;
    r->frame_prog = 0;
    r->frame_w = 0;
    r->frame_h = 0;
}

bool zcsr_gl_frame_bind(zcsr_gl_renderer* r) {
    if (r == NULL) return false;
    zcsr_gl_update_size(r);
    if (!zcsr_gl_frame_resize(r, r->width, r->height)) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, r->frame_fbo);
    zcsr_gl_set_framebuffer_srgb(false);
    glViewport(0, 0, r->width, r->height);
    return true;
}

bool zcsr_gl_bind_active_target(zcsr_gl_renderer* r) {
    zcsr_gl_tex_entry* e;
    if (r == NULL) return false;
    if (r->target == 0) return zcsr_gl_frame_bind(r);
    e = zcsr_gl_find_tex(r, r->target);
    if (e == NULL) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, r->target, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        return false;
    }
    if (e->color_space == ZCSR_COLOR_SPACE_LINEAR) {
        zcsr_gl_set_framebuffer_srgb(false);
    } else {
        zcsr_gl_set_framebuffer_srgb(true);
    }
    r->width = e->w;
    r->height = e->h;
    glViewport(0, 0, r->width, r->height);
    return true;
}

bool zcsr_gl_frame_encode(zcsr_gl_renderer* r) {
    GLboolean blend, depth, scissor;
    if (r == NULL || r->frame_tex == 0u || r->frame_prog == 0u ||
        r->l2d_color_lut_tex == 0u) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    zcsr_gl_update_size(r);
    if (r->width <= 0 || r->height <= 0) return false;
    glViewport(0, 0, r->width, r->height);
    blend = glIsEnabled(GL_BLEND);
    depth = glIsEnabled(GL_DEPTH_TEST);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    zcsr_gl_set_framebuffer_srgb(false);
    glUseProgram(r->frame_prog);
    glUniform1i(r->frame_loc_tex, 0);
    glUniform1i(r->frame_loc_lut, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->frame_tex);
    glActiveTexture(GL_TEXTURE0 + 1u);
    glBindTexture(GL_TEXTURE_2D, r->l2d_color_lut_tex);
    glBindVertexArray(r->frame_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    if (blend) glEnable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    return true;
}


/* glClear is affected by GL_SCISSOR_TEST. A clear call means "clear the whole
 * selected target", so temporarily disable scissor and restore the caller's
 * state before returning. */
static void zcsr_gl_clear_color_full(GLfloat red, GLfloat green, GLfloat blue,
                                     GLfloat alpha) {
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    glClearColor(red, green, blue, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
    if (scissor) glEnable(GL_SCISSOR_TEST);
}

static void zcsr_gl_clear_display_color(zcsr_gl_renderer* r,
                                        float red, float green,
                                        float blue, float alpha,
                                        bool decode_to_linear,
                                        GLfloat out[4]) {
    const zcsr_color_pipeline_desc* p;
    if (!decode_to_linear) {
        out[0] = red; out[1] = green; out[2] = blue; out[3] = alpha;
        return;
    }
    p = r != NULL && r->color_pipeline != NULL
            ? r->color_pipeline : zcsr_color_pipeline_default();
    out[0] = zcsr_color_pipeline_to_linear(p, red);
    out[1] = zcsr_color_pipeline_to_linear(p, green);
    out[2] = zcsr_color_pipeline_to_linear(p, blue);
    out[3] = alpha;
}

void zcsr_gl_clear(zcsr_gl_renderer* r, float red, float green,
                   float blue, float alpha) {
    GLfloat linear[4];
    if (!r || !zcsr_gl_make_current(r)) return;
    /* Clear the unified linear frame, not the display backbuffer. */
    r->target = 0;
    if (!zcsr_gl_frame_bind(r)) {
        ZCSR_LOG_ERROR("glrender", "clear: linear frame bind failed");
        return;
    }
    zcsr_gl_clear_display_color(r, red, green, blue, alpha, true, linear);
    zcsr_gl_clear_color_full(linear[0], linear[1], linear[2], linear[3]);
}

void zcsr_gl_clear_target(zcsr_gl_renderer* r, float red, float green,
                          float blue, float alpha) {
    GLfloat clear[4];
    bool decode = true;
    if (!r || !zcsr_gl_make_current(r)) return;
    if (r->target != 0) {
        zcsr_gl_tex_entry* e = zcsr_gl_find_tex(r, r->target);
        if (e != NULL && e->color_space == ZCSR_COLOR_SPACE_LINEAR) decode = false;
    }
    if (!zcsr_gl_bind_active_target(r)) {
        ZCSR_LOG_ERROR("glrender", "clear_target: target bind failed");
        return;
    }
    zcsr_gl_clear_display_color(r, red, green, blue, alpha, decode, clear);
    zcsr_gl_clear_color_full(clear[0], clear[1], clear[2], clear[3]);
}

static bool zcsr_gl_color_space_valid(zcsr_texture_color_space cs) {
    return cs == ZCSR_COLOR_SPACE_DISPLAY ||
           cs == ZCSR_COLOR_SPACE_SRGB ||
           cs == ZCSR_COLOR_SPACE_LINEAR;
}

static GLint zcsr_gl_internal_format(zcsr_texture_color_space cs) {
    /* DISPLAY and SRGB are sRGB-encoded storage: the sampler decodes them to
     * linear before the shader sees a texel. LINEAR stays raw RGBA8. */
    return cs == ZCSR_COLOR_SPACE_LINEAR ? GL_RGBA8 : GL_SRGB8_ALPHA8;
}

static void zcsr_gl_drain_gl_errors(void) {
    while (glGetError() != GL_NO_ERROR) { }
}

static bool zcsr_gl_take_gl_error(const char* what) {
    GLenum error = glGetError();
    if (error == GL_NO_ERROR) return false;
    ZCSR_LOG_ERROR("glrender", "%s failed (GL error 0x%04x)", what, (unsigned)error);
    zcsr_gl_drain_gl_errors();
    return true;
}

/* Apply the renderer's texture-quality filter after a successful upload.
 * mips=true means a complete mip chain exists and quality 2 may use it. */
static void zcsr_gl_apply_texture_filter(zcsr_gl_renderer* r, bool mips) {
    if (r->tex_quality >= 2 && mips) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        GLint f = (r->tex_quality >= 1) ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    }
}

/* Build mips when the quality preset requests them. A failed generation is
 * non-fatal: fall back to a non-mip filter instead of sampling a stale chain. */
static bool zcsr_gl_generate_mips(zcsr_gl_renderer* r) {
    if (r->tex_quality < 2 || glGenerateMipmap == NULL) return false;
    zcsr_gl_drain_gl_errors();
    glGenerateMipmap(GL_TEXTURE_2D);
    if (zcsr_gl_take_gl_error("glGenerateMipmap")) return false;
    return true;
}

zcsr_gl_texture zcsr_gl_texture_create_ex(zcsr_gl_renderer* r, const uint8_t* rgba,
                                          int w, int h,
                                          zcsr_texture_color_space color_space,
                                          bool has_alpha) {
    zcsr_gl_tex_entry* e;
    unsigned int tex = 0;
    bool mips;
    if (!r || !rgba || w <= 0 || h <= 0 || !zcsr_gl_color_space_valid(color_space) ||
        !zcsr_gl_make_current(r)) return 0;
    e = zcsr_gl_free_tex(r);
    if (!e) return 0;
    zcsr_gl_drain_gl_errors();
    glGenTextures(1, &tex);
    if (!tex || zcsr_gl_take_gl_error("glGenTextures")) {
        if (tex) glDeleteTextures(1, &tex);
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, zcsr_gl_internal_format(color_space), w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (zcsr_gl_take_gl_error("texture create glTexImage2D")) {
        glDeleteTextures(1, &tex);
        return 0;
    }
    mips = zcsr_gl_generate_mips(r);
    zcsr_gl_apply_texture_filter(r, mips);
    *e = (zcsr_gl_tex_entry){ tex, w, h, true, color_space, has_alpha };
    zcsr_tex_index_insert(r, tex, (uint8_t)(e - r->textures));
    return tex;
}

zcsr_gl_texture zcsr_gl_texture_create(zcsr_gl_renderer* r, const uint8_t* rgba, int w, int h) {
    /* Compatibility default: display-domain bytes, alpha-capable. */
    return zcsr_gl_texture_create_ex(r, rgba, w, h, ZCSR_COLOR_SPACE_DISPLAY, true);
}

bool zcsr_gl_texture_query_meta(const zcsr_gl_renderer* r, zcsr_gl_texture t,
                                zcsr_texture_color_space* out_color_space,
                                bool* out_has_alpha) {
    zcsr_gl_tex_entry* e = zcsr_gl_find_tex((zcsr_gl_renderer*)r, t);
    if (!e) return false;
    if (out_color_space) *out_color_space = e->color_space;
    if (out_has_alpha) *out_has_alpha = e->has_alpha;
    return true;
}

void zcsr_gl_texture_filter(zcsr_gl_renderer* r, zcsr_gl_texture t, bool linear) {
    if (!r || !zcsr_gl_find_tex(r, t) || !zcsr_gl_make_current(r)) return;
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
}

bool zcsr_gl_texture_upload(zcsr_gl_renderer* r, zcsr_gl_texture t, const uint8_t* rgba, int w, int h) {
    zcsr_gl_tex_entry* e = zcsr_gl_find_tex(r, t);
    if (!e || !rgba || w <= 0 || h <= 0 || !zcsr_gl_make_current(r)) return false;
    glBindTexture(GL_TEXTURE_2D, t);
    zcsr_gl_drain_gl_errors();
    glTexImage2D(GL_TEXTURE_2D, 0, zcsr_gl_internal_format(e->color_space), w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (zcsr_gl_take_gl_error("texture upload glTexImage2D")) return false;
    e->w = w;
    e->h = h;
    /* A new base level invalidates the old mip chain; regenerate it before the
     * next sampling call. */
    zcsr_gl_apply_texture_filter(r, zcsr_gl_generate_mips(r));
    return true;
}

void zcsr_gl_texture_destroy(zcsr_gl_renderer* r, zcsr_gl_texture t) {
    zcsr_gl_tex_entry* e = zcsr_gl_find_tex(r, t);
    if (!e || !zcsr_gl_make_current(r)) return;
    zcsr_tex_index_remove(r, (uint32_t)t);
    glDeleteTextures(1, &e->id);
    *e = (zcsr_gl_tex_entry){ 0 };
}

void zcsr_gl_set_target(zcsr_gl_renderer* r, zcsr_gl_texture target_or_zero) {
    zcsr_gl_tex_entry* e;
    if (!r || !zcsr_gl_make_current(r)) return;
    ZCSR_LOG_DEBUG("glrender", "set_target called: target=%d (0=linear frame)",
                   (int)target_or_zero);
    if (target_or_zero == 0) {
        r->target = 0;
        if (!zcsr_gl_frame_bind(r)) {
            ZCSR_LOG_ERROR("glrender", "set_target: linear frame bind failed");
            return;
        }
        ZCSR_LOG_DEBUG("glrender", "bound linear frame %dx%d", r->width, r->height);
        return;
    }
    e = zcsr_gl_find_tex(r, target_or_zero);
    if (!e) {
        ZCSR_LOG_ERROR("glrender", "set_target: texture %d not found", (int)target_or_zero);
        return;
    }
    r->target = target_or_zero;
    if (!zcsr_gl_bind_active_target(r)) {
        ZCSR_LOG_ERROR("glrender", "set_target: framebuffer incomplete, falling back");
        r->target = 0;
        zcsr_gl_frame_bind(r);
        return;
    }
    ZCSR_LOG_DEBUG("glrender", "bound RTT texture %d, %dx%d",
                   (int)target_or_zero, e->w, e->h);
}

/* ---- Scissor --------------------------------------------------------------------------------
 * UI passes a top-left-origin rect; GL scissors from the bottom-left. Clamp to the render
 * target so a panel dragged partly off-screen cannot produce an invalid (negative) box. */
void zcsr_gl_scissor(zcsr_gl_renderer* r, int x, int y, int w, int h)
{
    int gh, gy;

    if (r == NULL) return;
    if (w <= 0 || h <= 0) { glDisable(GL_SCISSOR_TEST); return; }

    gh = r->height;
    gy = gh - (y + h);
    if (gy < 0) { h += gy; gy = 0; }
    if (gy + h > gh) { h = gh - gy; }
    if (h <= 0) { glDisable(GL_SCISSOR_TEST); return; }

    if (x < 0) { w += x; x = 0; }
    if (x + w > r->width) { w = r->width - x; }
    if (w <= 0) { glDisable(GL_SCISSOR_TEST); return; }

    glEnable(GL_SCISSOR_TEST);
    glScissor(x, gy, w, h);
}

void zcsr_gl_scissor_off(zcsr_gl_renderer* r)
{
    (void)r;
    glDisable(GL_SCISSOR_TEST);
}
