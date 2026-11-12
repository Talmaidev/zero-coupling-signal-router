#ifndef ZCSR_UI_GL_BACKEND_H
#define ZCSR_UI_GL_BACKEND_H
/* OpenGL backend for the generic UI runtime. Translates backend-neutral draw
 * commands into gl_render primitives. Text is already resolved in the command
 * list; image resources are supplied by the host as a logical-key -> texture
 * table (no callbacks). Implemented in modules/ui. */
#include "zcsr/ui_draw.h"
#include "zcsr/gl_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Host-supplied image resource binding. */
typedef struct {
    zcsr_ui_key      resource_key;
    zcsr_gl_texture  texture;
} zcsr_ui_gl_resource;

typedef struct zcsr_font_atlas zcsr_font_atlas;

/* Optional: inject a TTF font atlas for text. When set, UI text is drawn with
 * the TTF atlas; when NULL (default), the built-in 8x8 bitmap font is used.
 * Caller owns the atlas lifetime. */
void zcsr_ui_gl_set_font_atlas(zcsr_font_atlas* atlas);

/* Render the command list. Must be called inside the renderer's frame, after
 * zcsr_gl_begin and before zcsr_gl_flush. */
void zcsr_ui_gl_render(zcsr_gl_renderer* renderer,
                       const zcsr_ui_draw_list* list,
                       const zcsr_ui_gl_resource* resources,
                       size_t resource_count);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_GL_BACKEND_H */
