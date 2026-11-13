#ifndef ZCSR_UI_GL_RENDER_V2_H
#define ZCSR_UI_GL_RENDER_V2_H
/* OpenGL rendering backend for ZCSR UI v2 runtime.
 * Translates the abstract draw command list into gl_render primitives.
 * Implemented in modules/ui/ui_gl_render_v2.c. */
#include "zcsr/ui_draw_v2.h"
#include "zcsr/gl_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Render all draw commands from the UI runtime.
 * Must be called inside zcsr_gl_begin / zcsr_gl_flush. */
/* Render the v2 draw-command list. `res` maps the host's resource_key -> GPU texture
 * (same table the v1 backend takes); pass NULL/0 when the frame draws no images. */
void zcsr_ui_gl_render_v2(zcsr_gl_renderer* renderer, const ZCSR_UI* ui,
                          const zcsr_ui_gl_resource* res, size_t res_count);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_GL_RENDER_V2_H */
