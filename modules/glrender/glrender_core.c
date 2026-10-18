/* glrender_core.c — shader/program/VAO-VBO-FBO/white-tex/texture CRUD/clear/set_target.
 * Platform-independent GL logic. Context must be current and glad loaded before core_init.
 */
#include "glrender_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
    fprintf(stderr, "zcsr-gl: %s: %s\n",
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
        "void main(){\n"
        "  vec4 texel = texture(u_tex, v_uv);\n"
        "  float amount = clamp(v_mod.y, 0.0, 1.0);\n"
        "  if (v_mod.x < 0.5) o_color = texel * v_color;\n"
        "  else if (v_mod.x < 1.5) o_color = mix(texel, v_color, amount);\n"
        "  else o_color = clamp(texel + v_color * amount, 0.0, 1.0);\n"
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

/* ===========================================================================================
 * Texture table (shared with sprite/instanced/text via internal.h)
 * O5: hash lookup (256-slot open addressing) replaces O(128) linear scan.
 * =========================================================================================== */

#define TEX_INDEX_SIZE 256
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

/* ===========================================================================================
 * Core init / destroy (does NOT touch instanced resources — caller coordinates lifecycle)
 * =========================================================================================== */

bool zcsr_gl_core_init(zcsr_gl_renderer* r) {
    r->program = zcsr_gl_make_program();
    if (!r->program) return false;
    r->uniform_view = glGetUniformLocation(r->program, "u_view");
    r->uniform_tex = glGetUniformLocation(r->program, "u_tex");

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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return r->vao != 0 && r->vbo != 0 && r->fbo != 0 && r->white_tex != 0;
}

void zcsr_gl_core_destroy(zcsr_gl_renderer* r) {
    size_t i;
    for (i = 0; i < ZCSR_GL_MAX_TEXTURES; ++i) {
        if (r->textures[i].in_use && r->textures[i].id) glDeleteTextures(1, &r->textures[i].id);
    }
    if (r->white_tex) glDeleteTextures(1, &r->white_tex);
    if (r->fbo) glDeleteFramebuffers(1, &r->fbo);
    if (r->vbo) glDeleteBuffers(1, &r->vbo);
    if (r->vao) glDeleteVertexArrays(1, &r->vao);
    if (r->program) glDeleteProgram(r->program);
}

/* ===========================================================================================
 * Public API: clear / texture CRUD / set_target
 * =========================================================================================== */

void zcsr_gl_clear(zcsr_gl_renderer* r, float red, float green,
                   float blue, float alpha) {
    if (!r || !zcsr_gl_make_current(r)) return;
    /* This entry-point specifically clears the window back buffer. */
    r->target = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    zcsr_gl_update_size(r);
    glViewport(0, 0, r->width, r->height);
    glClearColor(red, green, blue, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
}

zcsr_gl_texture zcsr_gl_texture_create(zcsr_gl_renderer* r, const uint8_t* rgba, int w, int h) {
    zcsr_gl_tex_entry* e;
    unsigned int tex = 0;
    if (!r || !rgba || w <= 0 || h <= 0 || !zcsr_gl_make_current(r)) return 0;
    e = zcsr_gl_free_tex(r);
    if (!e) return 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    *e = (zcsr_gl_tex_entry){ tex, w, h, true };
    zcsr_tex_index_insert(r, tex, (uint8_t)(e - r->textures));
    return tex;
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    e->w = w;
    e->h = h;
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
    if (target_or_zero == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        r->target = 0;
        zcsr_gl_update_size(r);
        return;
    }
    e = zcsr_gl_find_tex(r, target_or_zero);
    if (!e) return;
    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_or_zero, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        r->target = 0;
        zcsr_gl_update_size(r);
        return;
    }
    r->target = target_or_zero;
    r->width = e->w;
    r->height = e->h;
}
