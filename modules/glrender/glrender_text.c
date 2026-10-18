/* glrender_text.c — minimal 5x7 block glyph text rendering.
 * Uses sprite's zcsr_gl_push_quad + zcsr_gl_record_draw to queue glyph quads (white texture).
 */
#include "glrender_internal.h"

#include <stddef.h>

void zcsr_gl_draw_text(zcsr_gl_renderer* r, const char* utf8, float x, float y, float scale,
                       float cr, float cg, float cb, float ca) {
    float pen = x;
    const unsigned char* p;
    if (!r || !utf8 || scale <= 0.0f) return;
    for (p = (const unsigned char*)utf8; *p; ++p) {
        unsigned char ch = *p;
        if (ch == '\n') {
            pen = x;
            y += 8.0f * scale;
            continue;
        }
        if (ch < 32u) continue;
        /* Minimal block glyph: enough for scores/debug labels without external font data. */
        if (r->vertex_count + 6u <= ZCSR_GL_MAX_VERTICES) {
            size_t first = r->vertex_count;
            zcsr_gl_push_quad(r, (zcsr_rectf){ pen, y, 5.0f * scale, 7.0f * scale }, 0.0f,
                              cr, cg, cb, ca, ZCSR_MOD_MULTIPLY, 0.0f);
            zcsr_gl_record_draw(r, r->white_tex, first, r->vertex_count - first);
        }
        pen += 6.0f * scale;
    }
}
