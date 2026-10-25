/* glrender_l2d_geo.c — public G-buffer geometry submission (gl_render_l2d_geo.h).
 * One shared MRT program; renderer-owned resources, lazily initialized on the
 * first geo_begin and released from zcsr_gl_core_destroy. Never touches FBOs
 * or MRT draw buffers: callers must be inside an open light2d geometry phase
 * (zcsr_light2d_begin_geometry has bound the G-buffer). */
#include "glrender_internal.h"
#include "zcsr/gl_render_l2d_geo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* VS applies the shared game-shear projection; the ORIGINAL world position
 * and normal pass through as varyings — the G-buffer stores world data, the
 * projection decides screen placement only. k comes from the camera argument
 * (u_k); nothing here hardcodes a shear constant. */
static const char* geo_vs =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec3 a_norm;\n"
    "uniform vec2 u_cam;\n"
    "uniform float u_ppu;\n"
    "uniform float u_k;\n"
    "uniform vec2 u_view;\n"
    "out vec3 v_world;\n"
    "out vec3 v_norm;\n"
    "void main(){\n"
    "  float sx = (a_pos.x - u_cam.x) * u_ppu + u_view.x * 0.5;\n"
    "  float sy = (a_pos.y - u_cam.y - u_k * a_pos.z) * u_ppu + u_view.y * 0.5;\n"
    "  gl_Position = vec4(sx / u_view.x * 2.0 - 1.0, 1.0 - sy / u_view.y * 2.0, 0.0, 1.0);\n"
    "  v_world = a_pos;\n"
    "  v_norm = a_norm;\n"
    "}\n";

static const char* geo_fs =
    "#version 330 core\n"
    "in vec3 v_world;\n"
    "in vec3 v_norm;\n"
    "uniform vec4 u_albedo;\n"
    "uniform vec4 u_emissive;\n"
    "layout(location=0) out vec4 o_albedo;\n"
    "layout(location=1) out vec4 o_position;\n"
    "layout(location=2) out vec4 o_normal;\n"
    "layout(location=3) out vec4 o_emissive;\n"
    "void main(){\n"
    "  o_albedo = u_albedo;\n"
    "  o_position = vec4(v_world, 1.0);\n"
    "  o_normal = vec4(v_norm * 0.5 + 0.5, 1.0);\n"
    "  o_emissive = u_emissive;\n"
    "}\n";

/* Textured variant: same shear VS plus a UV channel; FS samples the host
 * texture on unit 0. Plain GL_RGBA textures carry the same encoding the
 * composite linearizes once (pow 2.2) — no second decode here. Cutout rule:
 * alpha < 0.5 discards so transparent pixels never overwrite the G-buffer
 * surface beneath (painter order supplies that surface). */
static const char* geo_tex_vs =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec3 a_norm;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "uniform vec2 u_cam;\n"
    "uniform float u_ppu;\n"
    "uniform float u_k;\n"
    "uniform vec2 u_view;\n"
    "out vec3 v_world;\n"
    "out vec3 v_norm;\n"
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  float sx = (a_pos.x - u_cam.x) * u_ppu + u_view.x * 0.5;\n"
    "  float sy = (a_pos.y - u_cam.y - u_k * a_pos.z) * u_ppu + u_view.y * 0.5;\n"
    "  gl_Position = vec4(sx / u_view.x * 2.0 - 1.0, 1.0 - sy / u_view.y * 2.0, 0.0, 1.0);\n"
    "  v_world = a_pos;\n"
    "  v_norm = a_norm;\n"
    "  v_uv = a_uv;\n"
    "}\n";

static const char* geo_tex_fs =
    "#version 330 core\n"
    "in vec3 v_world;\n"
    "in vec3 v_norm;\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;   /* unit 0, host-owned handle */\n"
    "uniform vec4 u_tint;\n"
    "uniform vec4 u_emissive;\n"
    "layout(location=0) out vec4 o_albedo;\n"
    "layout(location=1) out vec4 o_position;\n"
    "layout(location=2) out vec4 o_normal;\n"
    "layout(location=3) out vec4 o_emissive;\n"
    "void main(){\n"
    "  vec4 rgba = texture(u_tex, v_uv) * u_tint;\n"
    "  if (rgba.a < 0.5) discard;\n"
    "  o_albedo = rgba;\n"
    "  o_position = vec4(v_world, 1.0);\n"
    "  o_normal = vec4(v_norm * 0.5 + 0.5, 1.0);\n"
    "  o_emissive = u_emissive;\n"
    "}\n";

static unsigned int geo_compile(unsigned int type, const char* src) {
    unsigned int shader = glCreateShader(type);
    int ok = 0;
    if (!shader) return 0;
    glShaderSource(shader, 1, &src, 0);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), &len, log);
        if (len < 0) len = 0;
        if (len >= (GLsizei)sizeof(log)) len = (GLsizei)sizeof(log) - 1;
        log[len] = '\0';
        fprintf(stderr, "l2d_geo shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* Lazy one-time resource init: program + VAO/VBO (6 verts, pos+normal).
 * BOTH programs must exist for a clean bill; a partial init (flat ok, tex
 * failed) rolls back so the next begin retries from zero — otherwise
 * tex_face would silently draw with program 0. */
static bool geo_ensure(zcsr_gl_renderer* r) {
    unsigned int v;
    unsigned int f;
    unsigned int p;
    int ok = 0;
    if (r->l2d_geo_prog != 0u && r->l2d_geo_tex_prog != 0u) return true;
    if (r->l2d_geo_prog != 0u || r->l2d_geo_tex_prog != 0u) {
        zcsr_gl_l2d_geo_cleanup(r); /* roll back the partial set, retry clean */
    }
    v = geo_compile(GL_VERTEX_SHADER, geo_vs);
    f = geo_compile(GL_FRAGMENT_SHADER, geo_fs);
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
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(p, (GLsizei)sizeof(log), &len, log);
        if (len < 0) len = 0;
        if (len >= (GLsizei)sizeof(log)) len = (GLsizei)sizeof(log) - 1;
        log[len] = '\0';
        fprintf(stderr, "l2d_geo program link failed: %s\n", log);
        glDeleteProgram(p);
        return false;
    }
    r->l2d_geo_prog = p;
    r->l2d_geo_loc_cam = glGetUniformLocation(p, "u_cam");
    r->l2d_geo_loc_ppu = glGetUniformLocation(p, "u_ppu");
    r->l2d_geo_loc_k = glGetUniformLocation(p, "u_k");
    r->l2d_geo_loc_view = glGetUniformLocation(p, "u_view");
    r->l2d_geo_loc_albedo = glGetUniformLocation(p, "u_albedo");
    r->l2d_geo_loc_emissive = glGetUniformLocation(p, "u_emissive");
    glGenVertexArrays(1, &r->l2d_geo_vao);
    glGenBuffers(1, &r->l2d_geo_vbo);
    if (!r->l2d_geo_vao || !r->l2d_geo_vbo) return false;
    glBindVertexArray(r->l2d_geo_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->l2d_geo_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 6 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    /* textured variant (both programs up front — one lifecycle, one cleanup).
     * Any tex-side failure rolls the WHOLE set back (flat included) so the
     * next begin retries from zero — the ensure gate demands both. */
    v = geo_compile(GL_VERTEX_SHADER, geo_tex_vs);
    f = geo_compile(GL_FRAGMENT_SHADER, geo_tex_fs);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        zcsr_gl_l2d_geo_cleanup(r);
        return false;
    }
    p = glCreateProgram();
    if (!p) {
        glDeleteShader(v);
        glDeleteShader(f);
        zcsr_gl_l2d_geo_cleanup(r);
        return false;
    }
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(p, (GLsizei)sizeof(log), &len, log);
        if (len < 0) len = 0;
        if (len >= (GLsizei)sizeof(log)) len = (GLsizei)sizeof(log) - 1;
        log[len] = '\0';
        fprintf(stderr, "l2d_geo tex program link failed: %s\n", log);
        glDeleteProgram(p);
        zcsr_gl_l2d_geo_cleanup(r);
        return false;
    }
    r->l2d_geo_tex_prog = p;
    r->l2d_geo_tex_loc_cam = glGetUniformLocation(p, "u_cam");
    r->l2d_geo_tex_loc_ppu = glGetUniformLocation(p, "u_ppu");
    r->l2d_geo_tex_loc_k = glGetUniformLocation(p, "u_k");
    r->l2d_geo_tex_loc_view = glGetUniformLocation(p, "u_view");
    r->l2d_geo_tex_loc_tint = glGetUniformLocation(p, "u_tint");
    r->l2d_geo_tex_loc_emissive = glGetUniformLocation(p, "u_emissive");
    r->l2d_geo_tex_loc_sampler = glGetUniformLocation(p, "u_tex");
    glGenVertexArrays(1, &r->l2d_geo_tex_vao);
    glGenBuffers(1, &r->l2d_geo_tex_vbo);
    if (!r->l2d_geo_tex_vao || !r->l2d_geo_tex_vbo) return false;
    glBindVertexArray(r->l2d_geo_tex_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->l2d_geo_tex_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 8 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void*)(6 * sizeof(float)));
    glBindVertexArray(0);
    if (r->l2d_geo_tex_loc_sampler >= 0) {
        glUseProgram(r->l2d_geo_tex_prog);
        glUniform1i(r->l2d_geo_tex_loc_sampler, 0);
        glUseProgram(0);
    }
    return true;
}

/* Internal: release renderer-owned geo resources (core destroy path, GL current). */
void zcsr_gl_l2d_geo_cleanup(zcsr_gl_renderer* r) {
    if (r == NULL) return;
    if (r->l2d_geo_vbo) glDeleteBuffers(1, &r->l2d_geo_vbo);
    if (r->l2d_geo_vao) glDeleteVertexArrays(1, &r->l2d_geo_vao);
    if (r->l2d_geo_prog) glDeleteProgram(r->l2d_geo_prog);
    if (r->l2d_geo_tex_vbo) glDeleteBuffers(1, &r->l2d_geo_tex_vbo);
    if (r->l2d_geo_tex_vao) glDeleteVertexArrays(1, &r->l2d_geo_tex_vao);
    if (r->l2d_geo_tex_prog) glDeleteProgram(r->l2d_geo_tex_prog);
    r->l2d_geo_vbo = 0;
    r->l2d_geo_vao = 0;
    r->l2d_geo_prog = 0;
    r->l2d_geo_tex_vbo = 0;
    r->l2d_geo_tex_vao = 0;
    r->l2d_geo_tex_prog = 0;
    r->l2d_geo_active = false;
}

bool zcsr_gl_l2d_geo_begin(zcsr_gl_renderer* renderer,
                           const zcsr_camera* camera) {
    GLint viewport[4];
    if (renderer == NULL || camera == NULL) return false;
    if (!isfinite(camera->center_x) || !isfinite(camera->center_y) ||
        !isfinite(camera->px_per_unit) || camera->px_per_unit <= 0.0f ||
        !isfinite(camera->height_shear)) {
        return false;
    }
    if (renderer->l2d_geo_active) return false; /* nested begin */
    if (!zcsr_gl_make_current(renderer)) return false;
    if (!geo_ensure(renderer)) return false;

    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &renderer->l2d_geo_saved_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &renderer->l2d_geo_saved_vbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &renderer->l2d_geo_saved_prog);
    glGetIntegerv(GL_BLEND, &renderer->l2d_geo_saved_blend);
    glGetIntegerv(GL_DEPTH_TEST, &renderer->l2d_geo_saved_depth);
    glGetIntegerv(GL_CULL_FACE, &renderer->l2d_geo_saved_cull);
    /* unit 0 is the textured path's sampler slot: save active unit + binding */
    glGetIntegerv(GL_ACTIVE_TEXTURE, &renderer->l2d_geo_saved_active_tex);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &renderer->l2d_geo_saved_unit0_tex);

    glDisable(GL_BLEND);   /* opaque geometry into the G-buffer */
    glDisable(GL_DEPTH_TEST); /* no depth attachment: painter's order contract */
    glDisable(GL_CULL_FACE);  /* world-space winding, culling is caller's job */

    glGetIntegerv(GL_VIEWPORT, viewport);
    glUseProgram(renderer->l2d_geo_prog);
    if (renderer->l2d_geo_loc_view >= 0) {
        glUniform2f(renderer->l2d_geo_loc_view,
                    (float)viewport[2], (float)viewport[3]);
    }
    if (renderer->l2d_geo_loc_cam >= 0) {
        glUniform2f(renderer->l2d_geo_loc_cam, camera->center_x, camera->center_y);
    }
    if (renderer->l2d_geo_loc_ppu >= 0) {
        glUniform1f(renderer->l2d_geo_loc_ppu, camera->px_per_unit);
    }
    if (renderer->l2d_geo_loc_k >= 0) {
        glUniform1f(renderer->l2d_geo_loc_k, camera->height_shear);
    }
    /* the textured variant shares the same uniforms — keep both in sync */
    glUseProgram(renderer->l2d_geo_tex_prog);
    if (renderer->l2d_geo_tex_loc_view >= 0) {
        glUniform2f(renderer->l2d_geo_tex_loc_view,
                    (float)viewport[2], (float)viewport[3]);
    }
    if (renderer->l2d_geo_tex_loc_cam >= 0) {
        glUniform2f(renderer->l2d_geo_tex_loc_cam, camera->center_x, camera->center_y);
    }
    if (renderer->l2d_geo_tex_loc_ppu >= 0) {
        glUniform1f(renderer->l2d_geo_tex_loc_ppu, camera->px_per_unit);
    }
    if (renderer->l2d_geo_tex_loc_k >= 0) {
        glUniform1f(renderer->l2d_geo_tex_loc_k, camera->height_shear);
    }
    renderer->l2d_geo_active = true;
    return true;
}

bool zcsr_gl_l2d_geo_face(zcsr_gl_renderer* renderer,
                          const float corners_xyz[12],
                          const float albedo_rgba[4],
                          const float normal_xyz[3],
                          const float emissive_rgba[4]) {
    float verts[6 * 6];
    int i;
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };

    if (renderer == NULL || !renderer->l2d_geo_active) return false;
    if (corners_xyz == NULL || albedo_rgba == NULL ||
        normal_xyz == NULL || emissive_rgba == NULL) return false;
    for (i = 0; i < 12; ++i) {
        if (!isfinite(corners_xyz[i])) return false;
    }
    for (i = 0; i < 4; ++i) {
        if (!isfinite(albedo_rgba[i]) || !isfinite(emissive_rgba[i])) return false;
    }
    for (i = 0; i < 3; ++i) {
        if (!isfinite(normal_xyz[i])) return false;
    }
    /* per-face uniform color/emission; two triangles from the boundary order */
    for (i = 0; i < 6; ++i) {
        const int c = idx[i];
        verts[i * 6 + 0] = corners_xyz[c * 3 + 0];
        verts[i * 6 + 1] = corners_xyz[c * 3 + 1];
        verts[i * 6 + 2] = corners_xyz[c * 3 + 2];
        verts[i * 6 + 3] = normal_xyz[0];
        verts[i * 6 + 4] = normal_xyz[1];
        verts[i * 6 + 5] = normal_xyz[2];
    }
    glUseProgram(renderer->l2d_geo_prog);
    if (renderer->l2d_geo_loc_albedo >= 0) {
        glUniform4f(renderer->l2d_geo_loc_albedo,
                    albedo_rgba[0], albedo_rgba[1], albedo_rgba[2], albedo_rgba[3]);
    }
    if (renderer->l2d_geo_loc_emissive >= 0) {
        glUniform4f(renderer->l2d_geo_loc_emissive,
                    emissive_rgba[0], emissive_rgba[1],
                    emissive_rgba[2], emissive_rgba[3]);
    }
    glBindVertexArray(renderer->l2d_geo_vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->l2d_geo_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    return true;
}

bool zcsr_gl_l2d_geo_tex_face(zcsr_gl_renderer* renderer,
                              zcsr_gl_texture texture,
                              const float corners_xyz[12],
                              const float uv_st[8],
                              const float tint_rgba[4],
                              const float normal_xyz[3],
                              const float emissive_rgba[4]) {
    float verts[6 * 8];
    int i;
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    zcsr_gl_tex_entry* e;

    if (renderer == NULL || !renderer->l2d_geo_active) return false;
    if (renderer->l2d_geo_tex_prog == 0u) return false; /* partial init guard */
    if (corners_xyz == NULL || uv_st == NULL || tint_rgba == NULL ||
        normal_xyz == NULL || emissive_rgba == NULL) return false;
    e = zcsr_gl_find_tex(renderer, texture);
    if (e == NULL || e->id == 0u) return false; /* host-owned handle must be live */
    for (i = 0; i < 12; ++i) {
        if (!isfinite(corners_xyz[i])) return false;
    }
    for (i = 0; i < 8; ++i) {
        if (!isfinite(uv_st[i])) return false;
    }
    for (i = 0; i < 4; ++i) {
        if (!isfinite(tint_rgba[i]) || !isfinite(emissive_rgba[i])) return false;
    }
    for (i = 0; i < 3; ++i) {
        if (!isfinite(normal_xyz[i])) return false;
    }
    for (i = 0; i < 6; ++i) {
        const int c = idx[i];
        verts[i * 8 + 0] = corners_xyz[c * 3 + 0];
        verts[i * 8 + 1] = corners_xyz[c * 3 + 1];
        verts[i * 8 + 2] = corners_xyz[c * 3 + 2];
        verts[i * 8 + 3] = normal_xyz[0];
        verts[i * 8 + 4] = normal_xyz[1];
        verts[i * 8 + 5] = normal_xyz[2];
        verts[i * 8 + 6] = uv_st[c * 2 + 0];
        verts[i * 8 + 7] = uv_st[c * 2 + 1];
    }
    glUseProgram(renderer->l2d_geo_tex_prog);
    if (renderer->l2d_geo_tex_loc_tint >= 0) {
        glUniform4f(renderer->l2d_geo_tex_loc_tint,
                    tint_rgba[0], tint_rgba[1], tint_rgba[2], tint_rgba[3]);
    }
    if (renderer->l2d_geo_tex_loc_emissive >= 0) {
        glUniform4f(renderer->l2d_geo_tex_loc_emissive,
                    emissive_rgba[0], emissive_rgba[1],
                    emissive_rgba[2], emissive_rgba[3]);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, e->id);
    glBindVertexArray(renderer->l2d_geo_tex_vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->l2d_geo_tex_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    return true;
}

bool zcsr_gl_l2d_geo_end(zcsr_gl_renderer* renderer) {
    if (renderer == NULL || !renderer->l2d_geo_active) return false;
    if (!zcsr_gl_make_current(renderer)) return false;
    glUseProgram((GLuint)renderer->l2d_geo_saved_prog);
    glBindVertexArray((GLuint)renderer->l2d_geo_saved_vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)renderer->l2d_geo_saved_vbo);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)renderer->l2d_geo_saved_unit0_tex);
    glActiveTexture((GLenum)renderer->l2d_geo_saved_active_tex);
    if (renderer->l2d_geo_saved_blend != 0) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (renderer->l2d_geo_saved_depth != 0) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (renderer->l2d_geo_saved_cull != 0) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    renderer->l2d_geo_active = false;
    return true;
}
