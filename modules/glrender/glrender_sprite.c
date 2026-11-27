/* glrender_sprite.c — vertex queue, quad push, draw recording, flush, batch submission.
 * Uses core's texture table (zcsr_gl_find_tex) and platform hooks (make_current/swap/update_size).
 */
#include "glrender_internal.h"
#include "zcsr/font_atlas.h"
#include "zcsr/diagnostics.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* ===========================================================================================
 * Vertex queue / quad push / draw recording
 * =========================================================================================== */

void zcsr_gl_push_vertex(zcsr_gl_renderer* r, float x, float y, float u, float v,
                         float cr, float cg, float cb, float ca,
                         zcsr_color_mode mode, float amount) {
    zcsr_gl_vertex* out;
    if (!r || r->vertex_count >= ZCSR_GL_MAX_VERTICES) return;
    out = &r->vertices[r->vertex_count++];
    *out = (zcsr_gl_vertex){ x, y, u, v, cr, cg, cb, ca, (float)mode, amount };
}

void zcsr_gl_push_quad(zcsr_gl_renderer* r, zcsr_rectf dst, float rot,
                       float cr, float cg, float cb, float ca,
                       zcsr_color_mode mode, float amount) {
    float hw = dst.w * 0.5f;
    float hh = dst.h * 0.5f;
    float cx = dst.x + hw;
    float cy = dst.y + hh;
    float c = cosf(rot);
    float s = sinf(rot);
    float px[4] = { -hw, hw, hw, -hw };
    float py[4] = { -hh, -hh, hh, hh };
    float vx[4], vy[4];
    int i;
    for (i = 0; i < 4; ++i) {
        vx[i] = cx + px[i] * c - py[i] * s;
        vy[i] = cy + px[i] * s + py[i] * c;
    }
    zcsr_gl_push_vertex(r, vx[0], vy[0], 0.0f, 0.0f, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[1], vy[1], 1.0f, 0.0f, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[2], vy[2], 1.0f, 1.0f, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[0], vy[0], 0.0f, 0.0f, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[2], vy[2], 1.0f, 1.0f, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[3], vy[3], 0.0f, 1.0f, cr, cg, cb, ca, mode, amount);
}

/* Quad with an explicit UV sub-rectangle and optional mirroring. The UV corners
 * are swapped, not the destination rectangle: rotation/pivot stay unchanged. */
void zcsr_gl_push_quad_uv_flip(zcsr_gl_renderer* r, zcsr_rectf dst, float rot,
                               float u0, float v0, float u1, float v1,
                               float cr, float cg, float cb, float ca,
                               zcsr_color_mode mode, float amount,
                               bool flip_x, bool flip_y) {
    float hw = dst.w * 0.5f;
    float hh = dst.h * 0.5f;
    float cx = dst.x + hw;
    float cy = dst.y + hh;
    float c = cosf(rot);
    float s = sinf(rot);
    float px[4] = { -hw, hw, hw, -hw };
    float py[4] = { -hh, -hh, hh, hh };
    float vx[4], vy[4];
    float uu0 = flip_x ? u1 : u0;
    float uu1 = flip_x ? u0 : u1;
    float vv0 = flip_y ? v1 : v0;
    float vv1 = flip_y ? v0 : v1;
    int i;
    for (i = 0; i < 4; ++i) {
        vx[i] = cx + px[i] * c - py[i] * s;
        vy[i] = cy + px[i] * s + py[i] * c;
    }
    zcsr_gl_push_vertex(r, vx[0], vy[0], uu0, vv0, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[1], vy[1], uu1, vv0, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[2], vy[2], uu1, vv1, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[0], vy[0], uu0, vv0, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[2], vy[2], uu1, vv1, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[3], vy[3], uu0, vv1, cr, cg, cb, ca, mode, amount);
}

void zcsr_gl_push_quad_uv(zcsr_gl_renderer* r, zcsr_rectf dst, float rot,
                          float u0, float v0, float u1, float v1,
                          float cr, float cg, float cb, float ca,
                          zcsr_color_mode mode, float amount) {
    zcsr_gl_push_quad_uv_flip(r, dst, rot, u0, v0, u1, v1, cr, cg, cb, ca,
                              mode, amount, false, false);
}

void zcsr_gl_record_draw(zcsr_gl_renderer* r, unsigned int texture, size_t first, size_t count) {
    if (!r || r->draw_count >= ZCSR_GL_MAX_SPRITES || count == 0) return;
    r->draws[r->draw_count++] = (zcsr_gl_draw){ texture, first, count };
}

/* Emit one arbitrary quad. A concave quad has exactly one valid diagonal:
 * the one joining the neighbours of the reflex vertex. A convex quad uses
 * the 0-2 fan. A degenerate or self-intersecting quad is rejected. */
bool zcsr_gl_submit_quad(zcsr_gl_renderer* r, const zcsr_gl_quad* q) {
    float area = 0.0f;
    float eps;
    int reflex = -1;
    int i;
    int tri[2][3];
    zcsr_gl_texture tex;
    size_t first;

    if (!r || !q) return false;
    for (i = 0; i < 4; ++i) {
        if (!isfinite(q->x[i]) || !isfinite(q->y[i]) ||
            !isfinite(q->u[i]) || !isfinite(q->v[i])) return false;
    }
    if (!isfinite(q->r) || !isfinite(q->g) || !isfinite(q->b) || !isfinite(q->a)) return false;
    for (i = 0; i < 4; ++i) {
        int j = (i + 1) & 3;
        area += q->x[i] * q->y[j] - q->x[j] * q->y[i];
    }
    eps = 1e-6f * (fabsf(area) + 1.0f);
    if (fabsf(area) <= eps) return false;

    for (i = 0; i < 4; ++i) {
        int p = (i + 3) & 3;
        int n = (i + 1) & 3;
        float cross = (q->x[i] - q->x[p]) * (q->y[n] - q->y[i]) -
                      (q->y[i] - q->y[p]) * (q->x[n] - q->x[i]);
        if ((area > 0.0f && cross < -eps) || (area < 0.0f && cross > eps)) {
            reflex = i;
            break;
        }
    }
    if (reflex >= 0) {
        int p = (reflex + 3) & 3;
        int n = (reflex + 1) & 3;
        int nn = (reflex + 2) & 3;
        tri[0][0] = p; tri[0][1] = reflex; tri[0][2] = n;
        tri[1][0] = p; tri[1][1] = n;     tri[1][2] = nn;
    } else {
        tri[0][0] = 0; tri[0][1] = 1; tri[0][2] = 2;
        tri[1][0] = 0; tri[1][1] = 2; tri[1][2] = 3;
    }

    tex = q->tex ? q->tex : r->white_tex;
    if (!zcsr_gl_tex_path_a_compatible(r, tex)) {
        ZCSR_LOG_ERROR("glrender", "submit_quad: texture %u is not DISPLAY-encoded",
                       (unsigned)tex);
        return false;
    }
    if (r->vertex_count + 6u > ZCSR_GL_MAX_VERTICES) return false;
    if (r->draw_count + 1u > ZCSR_GL_MAX_SPRITES) return false;
    first = r->vertex_count;
    for (i = 0; i < 2; ++i) {
        int k;
        for (k = 0; k < 3; ++k) {
            int c = tri[i][k];
            zcsr_gl_push_vertex(r, q->x[c], q->y[c], q->u[c], q->v[c],
                                q->r, q->g, q->b, q->a, q->mode, q->amount);
        }
    }
    zcsr_gl_record_draw(r, tex, first, r->vertex_count - first);
    return true;
}

/* ===========================================================================================
 * Public API: begin / submit / flush / batch
 * =========================================================================================== */

void zcsr_gl_begin(zcsr_gl_renderer* r) {
    if (r) {
        r->vertex_count = 0;
        r->draw_count = 0;
        /* TTF atlas tracks the codepoints used per frame so it can rebuild on overflow;
         * this is the frame boundary it expects. NULL atlas = built-in bitmap font. */
        if (r->font_atlas) zcsr_font_atlas_begin_frame(r->font_atlas);
    }
}

/* Normalize a destination rect for the display path. A negative extent is a
 * mirror request, not negative GL geometry. */
static bool zcsr_gl_normalize_sprite_rect(zcsr_rectf* dst, bool* flip_x, bool* flip_y) {
    if (!isfinite(dst->x) || !isfinite(dst->y) ||
        !isfinite(dst->w) || !isfinite(dst->h)) return false;
    if (dst->w < 0.0f) {
        dst->x += dst->w;
        dst->w = -dst->w;
        if (flip_x) *flip_x = !*flip_x;
    }
    if (dst->h < 0.0f) {
        dst->y += dst->h;
        dst->h = -dst->h;
        if (flip_y) *flip_y = !*flip_y;
    }
    return dst->w > 0.0f && dst->h > 0.0f;
}

void zcsr_gl_submit(zcsr_gl_renderer* r, const zcsr_gl_sprite* s) {
    size_t first;
    zcsr_rectf dst;
    bool flip_x, flip_y;
    if (!r || !s || s->tex == 0) return;
    if (!zcsr_gl_tex_path_a_compatible(r, s->tex)) {
        ZCSR_LOG_ERROR("glrender", "submit: texture %u is not DISPLAY-encoded",
                       (unsigned)s->tex);
        return;
    }
    if (!isfinite(s->rotation) || !isfinite(s->r) || !isfinite(s->g) ||
        !isfinite(s->b) || !isfinite(s->a) || !isfinite(s->amount)) return;
    dst = s->dst;
    flip_x = s->flip_x;
    flip_y = s->flip_y;
    if (!zcsr_gl_normalize_sprite_rect(&dst, &flip_x, &flip_y)) return;
    if (r->vertex_count + 6u > ZCSR_GL_MAX_VERTICES) return;
    first = r->vertex_count;
    zcsr_gl_push_quad_uv_flip(r, dst, s->rotation, 0.0f, 0.0f, 1.0f, 1.0f,
                              s->r, s->g, s->b, s->a, s->mode, s->amount,
                              flip_x, flip_y);
    zcsr_gl_record_draw(r, s->tex, first, r->vertex_count - first);
}

void zcsr_gl_flush(zcsr_gl_renderer* r) {
    size_t i;
    if (!r || r->vertex_count == 0 || !zcsr_gl_make_current(r)) return;
    zcsr_gl_state_cache_invalidate(r); /* sprite path binds its own program/VAO/texture */
    if (!zcsr_gl_bind_active_target(r)) {
        ZCSR_LOG_ERROR("glrender", "flush: active target bind failed");
        return;
    }
    glViewport(0, 0, r->width, r->height);
    glUseProgram(r->program);
    glUniform2f(r->uniform_view, (float)r->width, (float)r->height);
    glUniform1i(r->uniform_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(r->vertex_count * sizeof(zcsr_gl_vertex)), r->vertices);
    /* Coalesce contiguous runs that share a texture into one draw call. The bench submits
     * thousands of same-texture sprites; issuing one draw per sprite is driver-call bound. */
    i = 0;
    while (i < r->draw_count) {
        size_t j = i;
        size_t count = r->draws[i].count;
        while (j + 1 < r->draw_count
               && r->draws[j + 1].texture == r->draws[i].texture
               && r->draws[j + 1].first == r->draws[j].first + r->draws[j].count) {
            count += r->draws[j + 1].count;
            ++j;
        }
        glBindTexture(GL_TEXTURE_2D, r->draws[i].texture);
        glDrawArrays(GL_TRIANGLES, (GLint)r->draws[i].first, (GLsizei)count);
        i = j + 1;
    }
    r->vertex_count = 0;
    r->draw_count = 0;
}

void zcsr_gl_submit_batch(zcsr_gl_renderer* r, const zcsr_gl_sprite* sprites, size_t count) {
    size_t i;
    if (!r || !sprites) return;
    zcsr_gl_begin(r);
    for (i = 0; i < count; ++i) zcsr_gl_submit(r, &sprites[i]);
    zcsr_gl_flush(r);
}

static void zcsr_gl_submit_atlas(zcsr_gl_renderer* r, const zcsr_gl_atlas_sprite* s) {
    size_t first;
    zcsr_rectf dst;
    bool flip_x, flip_y;
    if (!r || !s || s->atlas == 0) return;
    if (!zcsr_gl_tex_path_a_compatible(r, s->atlas)) {
        ZCSR_LOG_ERROR("glrender", "atlas submit: texture %u is not DISPLAY-encoded",
                       (unsigned)s->atlas);
        return;
    }
    if (!isfinite(s->rotation) || !isfinite(s->r) || !isfinite(s->g) ||
        !isfinite(s->b) || !isfinite(s->a) || !isfinite(s->amount)) return;
    dst = s->dst;
    flip_x = s->flip_x;
    flip_y = s->flip_y;
    if (!zcsr_gl_normalize_sprite_rect(&dst, &flip_x, &flip_y)) return;
    if (r->vertex_count + 6u > ZCSR_GL_MAX_VERTICES) return;
    first = r->vertex_count;
    zcsr_gl_push_quad_uv_flip(r, dst, s->rotation,
                              s->region.u0, s->region.v0, s->region.u1, s->region.v1,
                              s->r, s->g, s->b, s->a, s->mode, s->amount,
                              flip_x, flip_y);
    zcsr_gl_record_draw(r, s->atlas, first, r->vertex_count - first);
}

void zcsr_gl_submit_atlas_batch(zcsr_gl_renderer* r, const zcsr_gl_atlas_sprite* sprites,
                                size_t count) {
    size_t i;
    if (!r || !sprites) return;
    zcsr_gl_begin(r);
    for (i = 0; i < count; ++i) zcsr_gl_submit_atlas(r, &sprites[i]);
    zcsr_gl_flush(r);
}

/* ===========================================================================================
 * Basic geometry (white-texture quads, enqueue-only)
 * =========================================================================================== */

void zcsr_gl_fill_rect(zcsr_gl_renderer* r,
                       float x, float y, float w, float h,
                       float cr, float cg, float cb, float ca) {
    size_t first;
    if (!r || w <= 0.0f || h <= 0.0f) return;
    if (r->vertex_count + 6u > ZCSR_GL_MAX_VERTICES) return;
    first = r->vertex_count;
    zcsr_gl_push_quad(r, (zcsr_rectf){ x, y, w, h }, 0.0f,
                      cr, cg, cb, ca, ZCSR_MOD_MULTIPLY, 0.0f);
    zcsr_gl_record_draw(r, r->white_tex, first, r->vertex_count - first);
}

void zcsr_gl_draw_line(zcsr_gl_renderer* r,
                       float x1, float y1, float x2, float y2,
                       float width,
                       float cr, float cg, float cb, float ca) {
    float dx, dy, len, cx, cy, rot;
    size_t first;
    if (!r || width <= 0.0f) return;
    dx = x2 - x1;
    dy = y2 - y1;
    len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.0f) return;
    if (r->vertex_count + 6u > ZCSR_GL_MAX_VERTICES) return;
    cx = (x1 + x2) * 0.5f;
    cy = (y1 + y2) * 0.5f;
    rot = atan2f(dy, dx);
    first = r->vertex_count;
    zcsr_gl_push_quad(r, (zcsr_rectf){ cx - len * 0.5f, cy - width * 0.5f, len, width }, rot,
                      cr, cg, cb, ca, ZCSR_MOD_MULTIPLY, 0.0f);
    zcsr_gl_record_draw(r, r->white_tex, first, r->vertex_count - first);
}

void zcsr_gl_draw_rect(zcsr_gl_renderer* r,
                       float x, float y, float w, float h,
                       float thickness,
                       float cr, float cg, float cb, float ca) {
    if (!r || w <= 0.0f || h <= 0.0f || thickness <= 0.0f) return;
    /* Degenerate: outline thicker than the rectangle itself → filled rect. */
    if (w <= 2.0f * thickness || h <= 2.0f * thickness) {
        zcsr_gl_fill_rect(r, x, y, w, h, cr, cg, cb, ca);
        return;
    }
    /* Allocate all four edges atomically: 24 vertices + 4 draw records. */
    if (r->vertex_count + 24u > ZCSR_GL_MAX_VERTICES) return;
    if (r->draw_count + 4u > ZCSR_GL_MAX_SPRITES) return;
    zcsr_gl_fill_rect(r, x, y, w, thickness, cr, cg, cb, ca);
    zcsr_gl_fill_rect(r, x, y + h - thickness, w, thickness, cr, cg, cb, ca);
    zcsr_gl_fill_rect(r, x, y + thickness, thickness, h - 2.0f * thickness, cr, cg, cb, ca);
    zcsr_gl_fill_rect(r, x + w - thickness, y + thickness, thickness, h - 2.0f * thickness, cr, cg, cb, ca);
}

/* ===========================================================================================
 * Texture upload region / atlas sprite / filled circle (additive)
 * =========================================================================================== */

bool zcsr_gl_texture_upload_region(zcsr_gl_renderer* r, zcsr_gl_texture t,
                                    int x, int y, int w, int h,
                                    int src_stride_bytes,
                                    const uint8_t* rgba) {
    zcsr_gl_tex_entry* e;
    long long need_row, end_x, end_y;
    GLint old_alignment = 4;
#if defined(GL_UNPACK_ROW_LENGTH)
    GLint old_row_length = 0;
#endif
    if (!r || !rgba) return false;
    if (w <= 0 || h <= 0 || x < 0 || y < 0) return false;
    if (src_stride_bytes <= 0) return false;
    need_row = (long long)w * 4LL;
    if ((long long)src_stride_bytes < need_row) return false;
    e = zcsr_gl_find_tex(r, t);
    if (!e) return false;
    end_x = (long long)x + (long long)w;
    end_y = (long long)y + (long long)h;
    if (end_x > (long long)e->w) return false;
    if (end_y > (long long)e->h) return false;
    if (!zcsr_gl_make_current(r)) return false;
    while (glGetError() != GL_NO_ERROR) { }
    glBindTexture(GL_TEXTURE_2D, t);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &old_alignment);
#if defined(GL_UNPACK_ROW_LENGTH)
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &old_row_length);
#endif
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if ((src_stride_bytes % 4) == 0) {
#if defined(GL_UNPACK_ROW_LENGTH)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, src_stride_bytes / 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h,
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba);
#else
        for (int row = 0; row < h; ++row)
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y + row, w, 1,
                             GL_RGBA, GL_UNSIGNED_BYTE,
                             rgba + (size_t)row * src_stride_bytes);
#endif
    } else {
        for (int row = 0; row < h; ++row)
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y + row, w, 1,
                             GL_RGBA, GL_UNSIGNED_BYTE,
                             rgba + (size_t)row * src_stride_bytes);
    }
#if defined(GL_UNPACK_ROW_LENGTH)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, old_row_length);
#endif
    glPixelStorei(GL_UNPACK_ALIGNMENT, old_alignment);
    return glGetError() == GL_NO_ERROR;
}

void zcsr_gl_queue_atlas_sprite(zcsr_gl_renderer* r, zcsr_gl_texture atlas,
                                 float dx, float dy, float dw, float dh,
                                 float u0, float v0, float u1, float v1,
                                 float cr, float cg, float cb, float ca) {
    zcsr_gl_queue_atlas_sprite_rot(r, atlas, dx, dy, dw, dh, 0.0f,
                                   u0, v0, u1, v1, cr, cg, cb, ca, false, false);
}

void zcsr_gl_queue_atlas_sprite_rot(zcsr_gl_renderer* r, zcsr_gl_texture atlas,
                                     float dx, float dy, float dw, float dh,
                                     float rotation,
                                     float u0, float v0, float u1, float v1,
                                     float cr, float cg, float cb, float ca,
                                     bool flip_x, bool flip_y) {
    size_t first;
    zcsr_rectf dst;
    if (!r || atlas == 0 || !zcsr_gl_tex_path_a_compatible(r, atlas)) return;
    if (!isfinite(rotation) || !isfinite(cr) || !isfinite(cg) ||
        !isfinite(cb) || !isfinite(ca)) return;
    dst = (zcsr_rectf){ dx, dy, dw, dh };
    if (!zcsr_gl_normalize_sprite_rect(&dst, &flip_x, &flip_y)) return;
    if (r->vertex_count + 6u > ZCSR_GL_MAX_VERTICES) return;
    first = r->vertex_count;
    zcsr_gl_push_quad_uv_flip(r, dst, rotation, u0, v0, u1, v1,
                              cr, cg, cb, ca, ZCSR_MOD_MULTIPLY, 0.0f,
                              flip_x, flip_y);
    zcsr_gl_record_draw(r, atlas, first, r->vertex_count - first);
}

void zcsr_gl_fill_circle(zcsr_gl_renderer* r,
                          float cx, float cy, float radius, int segments,
                          float cr, float cg, float cb, float ca) {
    size_t first;
    int i;
    if (!r) return;
    if (!isfinite(cx) || !isfinite(cy) || !isfinite(radius)) return;
    if (radius <= 0.0f) return;
    if (!isfinite(cr) || !isfinite(cg) || !isfinite(cb) || !isfinite(ca)) return;
    if (segments < 32) segments = 32;
    if (segments > 256) segments = 256;
    if (r->vertex_count + (size_t)segments * 3u > ZCSR_GL_MAX_VERTICES) return;
    if (r->draw_count + 1u > ZCSR_GL_MAX_SPRITES) return;
    first = r->vertex_count;
    for (i = 0; i < segments; ++i) {
        float a0 = (float)i * 6.28318530718f / (float)segments;
        float a1 = (float)(i + 1) * 6.28318530718f / (float)segments;
        float x0 = cx + cosf(a0) * radius;
        float y0 = cy + sinf(a0) * radius;
        float x1 = cx + cosf(a1) * radius;
        float y1 = cy + sinf(a1) * radius;
        zcsr_gl_push_vertex(r, cx, cy, 0.5f, 0.5f, cr, cg, cb, ca, ZCSR_MOD_MULTIPLY, 0.0f);
        zcsr_gl_push_vertex(r, x0, y0, 0.0f, 0.0f, cr, cg, cb, ca, ZCSR_MOD_MULTIPLY, 0.0f);
        zcsr_gl_push_vertex(r, x1, y1, 1.0f, 1.0f, cr, cg, cb, ca, ZCSR_MOD_MULTIPLY, 0.0f);
    }
    zcsr_gl_record_draw(r, r->white_tex, first, r->vertex_count - first);
}
