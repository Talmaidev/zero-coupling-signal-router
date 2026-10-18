/* glrender_instanced.c — instanced rendering program, init/destroy, draw, radix sort, state sort.
 * Uses core's texture table (zcsr_gl_find_tex) and platform hooks (make_current/swap/update_size).
 */
#include "glrender_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_color;\n"
        "out vec2 v_mod;\n"
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

/* ===========================================================================================
 * Instanced init / destroy
 * =========================================================================================== */

bool zcsr_gl_instanced_init(zcsr_gl_renderer* r) {
    static const float corners[8] = {
        -1.f, -1.f,   1.f, -1.f,   1.f, 1.f,   -1.f, 1.f
    };
    static const unsigned short indices[6] = { 0, 1, 2, 0, 2, 3 };
    const size_t stride = sizeof(zcsr_gl_instance);

    r->inst_program = zcsr_gl_make_inst_program();
    if (!r->inst_program) return false;
    r->inst_uniform_view = glGetUniformLocation(r->inst_program, "u_view");
    r->inst_uniform_tex  = glGetUniformLocation(r->inst_program, "u_tex");

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
    glBufferData(GL_ARRAY_BUFFER, ZCSR_GL_MAX_INSTANCES * stride, NULL, GL_DYNAMIC_DRAW);

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

    glBindVertexArray(0);
    return r->inst_vao != 0 && r->inst_vbo != 0;
}

void zcsr_gl_instanced_destroy(zcsr_gl_renderer* r) {
    if (r->inst_vbo) glDeleteBuffers(1, &r->inst_vbo);
    if (r->inst_ebo) glDeleteBuffers(1, &r->inst_ebo);
    if (r->inst_corner_vbo) glDeleteBuffers(1, &r->inst_corner_vbo);
    if (r->inst_vao) glDeleteVertexArrays(1, &r->inst_vao);
    if (r->inst_program) glDeleteProgram(r->inst_program);
}

/* ===========================================================================================
 * Public API: draw_instances
 * =========================================================================================== */

/* Internal: per-run draw with GL state already set up (context current, framebuffer/viewport/
 * program/uniforms/VAO bound). Only uploads VBO, binds texture, issues draw. No find_tex. */
static void zcsr_gl_draw_instances_prepared(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                                             const zcsr_gl_instance* instances, size_t count) {
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
#ifdef ZCSR_GL_VBO_ORPHAN
    /* Orphaning: discard old storage before partial upload to avoid GPU sync stall. */
    glBufferData(GL_ARRAY_BUFFER, ZCSR_GL_MAX_INSTANCES * sizeof(zcsr_gl_instance), NULL,
                 GL_DYNAMIC_DRAW);
#endif
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(zcsr_gl_instance), instances);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, (GLsizei)count);
}

void zcsr_gl_draw_instances(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                            const zcsr_gl_instance* instances, size_t count) {
    if (!r || !instances || count == 0 || texture == 0) return;
    if (!zcsr_gl_find_tex(r, texture)) return;
    if (count > ZCSR_GL_MAX_INSTANCES) count = ZCSR_GL_MAX_INSTANCES;
    if (!zcsr_gl_make_current(r)) return;

    if (r->target == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        zcsr_gl_update_size(r);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               r->target, 0);
    }
    glViewport(0, 0, r->width, r->height);

    glUseProgram(r->inst_program);
    glUniform2f(r->inst_uniform_view, (float)r->width, (float)r->height);
    glUniform1i(r->inst_uniform_tex, 0);

    glBindVertexArray(r->inst_vao);
    zcsr_gl_draw_instances_prepared(r, texture, instances, count);
    glBindVertexArray(0);
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

void zcsr_gl_submit_instance_batch(zcsr_gl_renderer* r, zcsr_gl_instance* instances,
                                   size_t count, void* temp, size_t temp_bytes) {
    size_t runs, i;
    if (!r || !instances || !temp || count == 0) return;
    if (count > ZCSR_GL_MAX_INSTANCES) return;
    if (temp_bytes < count * sizeof(zcsr_gl_instance)) return;
    runs = zcsr_sort_instances(instances, count, temp, temp_bytes);

    /* Set up shared GL state once for all texture runs (O2: state hoisting). */
    if (!zcsr_gl_make_current(r)) return;
    if (r->target == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        zcsr_gl_update_size(r);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               r->target, 0);
    }
    glViewport(0, 0, r->width, r->height);
    glUseProgram(r->inst_program);
    glUniform2f(r->inst_uniform_view, (float)r->width, (float)r->height);
    glUniform1i(r->inst_uniform_tex, 0);
    glBindVertexArray(r->inst_vao);

    for (i = 0; i < runs; ++i) {
        zcsr_gl_texture tex;
        size_t start, len;
        zcsr_instance_run(instances, count, i, &tex, &start, &len);
        if (tex && zcsr_gl_find_tex(r, tex))
            zcsr_gl_draw_instances_prepared(r, tex, instances + start, len);
    }
    glBindVertexArray(0);
    if (r->target == 0) zcsr_gl_swap(r);
}

/* ===========================================================================================
 * Full state sort (opaque vs translucent)
 * =========================================================================================== */

static int zcsr_is_translucent(const zcsr_gl_instance* in) {
    return in->a < 0.9999f ||
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

void zcsr_gl_submit_state(zcsr_gl_renderer* r, zcsr_gl_instance* instances, size_t count,
                          void* temp, size_t temp_bytes) {
    size_t opaque;
    size_t i;
    if (!r || !instances || !temp || count == 0) return;
    if (count > ZCSR_GL_MAX_INSTANCES) return;
    if (temp_bytes < count * sizeof(zcsr_gl_instance)) return;
    zcsr_sort_state(instances, count, temp, temp_bytes, &opaque);

    /* opaque: draw in texture runs (order irrelevant) */
    i = 0;
    while (i < opaque) {
        size_t start = i;
        uint32_t tex = instances[i].tex;
        while (i < opaque && instances[i].tex == tex) ++i;
        if (tex) zcsr_gl_draw_instances(r, tex, instances + start, i - start);
    }
    /* translucent: back-to-front, gather only adjacent same-texture (z order preserved) */
    i = opaque;
    while (i < count) {
        size_t start = i;
        uint32_t tex = instances[i].tex;
        while (i < count && instances[i].tex == tex) ++i;
        if (tex) zcsr_gl_draw_instances(r, tex, instances + start, i - start);
    }
    if (r->target == 0) zcsr_gl_swap(r);
}
