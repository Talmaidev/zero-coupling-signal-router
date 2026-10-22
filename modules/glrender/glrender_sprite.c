/* glrender_sprite.c — vertex queue, quad push, draw recording, flush, batch submission.
 * Uses core's texture table (zcsr_gl_find_tex) and platform hooks (make_current/swap/update_size).
 */
#include "glrender_internal.h"

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

/* Quad with an explicit UV sub-rectangle (atlas sprites). Same geometry as push_quad but the
 * four corners sample [u0,u1] x [v0,v1] instead of the full texture. */
void zcsr_gl_push_quad_uv(zcsr_gl_renderer* r, zcsr_rectf dst, float rot,
                          float u0, float v0, float u1, float v1,
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
    zcsr_gl_push_vertex(r, vx[0], vy[0], u0, v0, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[1], vy[1], u1, v0, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[2], vy[2], u1, v1, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[0], vy[0], u0, v0, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[2], vy[2], u1, v1, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[3], vy[3], u0, v1, cr, cg, cb, ca, mode, amount);
}

void zcsr_gl_record_draw(zcsr_gl_renderer* r, unsigned int texture, size_t first, size_t count) {
    if (!r || r->draw_count >= ZCSR_GL_MAX_SPRITES || count == 0) return;
    r->draws[r->draw_count++] = (zcsr_gl_draw){ texture, first, count };
}

/* ===========================================================================================
 * Public API: begin / submit / flush / batch
 * =========================================================================================== */

void zcsr_gl_begin(zcsr_gl_renderer* r) {
    if (r) {
        r->vertex_count = 0;
        r->draw_count = 0;
    }
}

void zcsr_gl_submit(zcsr_gl_renderer* r, const zcsr_gl_sprite* s) {
    size_t first;
    if (!r || !s || s->tex == 0 || !zcsr_gl_find_tex(r, s->tex)) return;
    if (r->vertex_count + 6u > ZCSR_GL_MAX_VERTICES) return;
    first = r->vertex_count;
    zcsr_gl_push_quad(r, s->dst, s->rotation, s->r, s->g, s->b, s->a, s->mode, s->amount);
    zcsr_gl_record_draw(r, s->tex, first, r->vertex_count - first);
}

void zcsr_gl_flush(zcsr_gl_renderer* r) {
    size_t i;
    if (!r || r->vertex_count == 0 || !zcsr_gl_make_current(r)) return;
    if (r->target == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        zcsr_gl_update_size(r);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->target, 0);
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
    if (!r || !s || s->atlas == 0 || !zcsr_gl_find_tex(r, s->atlas)) return;
    if (r->vertex_count + 6u > ZCSR_GL_MAX_VERTICES) return;
    first = r->vertex_count;
    zcsr_gl_push_quad_uv(r, s->dst, s->rotation,
                         s->region.u0, s->region.v0, s->region.u1, s->region.v1,
                         s->r, s->g, s->b, s->a, s->mode, s->amount);
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
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if ((src_stride_bytes % 4) == 0) {
#if defined(GL_UNPACK_ROW_LENGTH)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, src_stride_bytes / 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h,
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
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
    return true;
}

void zcsr_gl_queue_atlas_sprite(zcsr_gl_renderer* r, zcsr_gl_texture atlas,
                                 float dx, float dy, float dw, float dh,
                                 float u0, float v0, float u1, float v1,
                                 float cr, float cg, float cb, float ca) {
    size_t first;
    if (!r || atlas == 0 || !zcsr_gl_find_tex(r, atlas)) return;
    if (r->vertex_count + 6u > ZCSR_GL_MAX_VERTICES) return;
    first = r->vertex_count;
    zcsr_gl_push_quad_uv(r, (zcsr_rectf){dx, dy, dw, dh}, 0.0f,
                          u0, v0, u1, v1, cr, cg, cb, ca,
                          ZCSR_MOD_MULTIPLY, 0.0f);
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
