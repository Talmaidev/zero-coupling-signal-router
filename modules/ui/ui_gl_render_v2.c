/* modules/ui/ui_gl_render_v2.c — OpenGL rendering backend for ZCSR UI v2.
 *
 * Translates the ZCSR_DrawCommand list (ui_draw_v2.h) into gl_render primitives.
 * Image 走宿主资源表（resource_key -> texture），找不到就画洋红 + 边框作为"缺资源"的
 * 明确可视信号 —— 不再用灰块假装有图。Clip 走真 scissor，带 8 层嵌套栈，滚动视图
 * 里再嵌面板不会丢外层裁剪。
 */
#include "zcsr/ui_draw_v2.h"
#include "zcsr/ui_gl_backend.h"
#include "zcsr/gl_render.h"

#include <stddef.h>
#include <stdint.h>

#define ZCSR_UI_CLIP_STACK_DEPTH 8

static zcsr_gl_texture lookup(const zcsr_ui_gl_resource* res, size_t count, uint32_t key)
{
    size_t i;
    if (res == NULL || key == 0u) return 0;
    for (i = 0; i < count; ++i) {
        if (res[i].resource_key == key) return res[i].texture;
    }
    return 0;
}

void zcsr_ui_gl_render_v2(zcsr_gl_renderer* renderer, const ZCSR_UI* ui,
                          const zcsr_ui_gl_resource* res, size_t res_count)
{
    /* Clip stack: intersect-on-push, restore-on-pop. Depth is bounded so a malformed
     * push/pop stream degrades to "no clip" instead of scribbling memory. */
    int clip_x[ZCSR_UI_CLIP_STACK_DEPTH], clip_y[ZCSR_UI_CLIP_STACK_DEPTH];
    int clip_w[ZCSR_UI_CLIP_STACK_DEPTH], clip_h[ZCSR_UI_CLIP_STACK_DEPTH];
    int clip_depth = 0;
    uint32_t cmd_count = 0;
    uint32_t i;

    if (renderer == NULL || ui == NULL) return;
    zcsr_ui_get_draw_command_count(ui, &cmd_count);

    for (i = 0; i < cmd_count; i++) {
        ZCSR_DrawCommand cmd;
        float x, y, w, h, cr, cg, cb, ca;

        zcsr_ui_get_draw_command(ui, i, &cmd);

        x = cmd.rect.x;
        y = cmd.rect.y;
        w = cmd.rect.width;
        h = cmd.rect.height;
        cr = cmd.color.r;
        cg = cmd.color.g;
        cb = cmd.color.b;
        ca = cmd.color.a;

        switch (cmd.type) {
        case ZCSR_DRAW_RECT:
            zcsr_gl_fill_rect(renderer, x, y, w, h, cr, cg, cb, ca);
            break;

        case ZCSR_DRAW_BORDER:
            zcsr_gl_draw_rect(renderer, x, y, w, h, cmd.thickness, cr, cg, cb, ca);
            break;

        case ZCSR_DRAW_TEXT:
            if (cmd.text_utf8 && cmd.text_bytes > 0) {
                zcsr_gl_draw_text(renderer, cmd.text_utf8, x, y, 1.0f, cr, cg, cb, ca);
            }
            break;

        case ZCSR_DRAW_LINE:
            zcsr_gl_draw_line(renderer, x, y, w, h, cmd.thickness, cr, cg, cb, ca);
            break;

        case ZCSR_DRAW_IMAGE: {
            zcsr_gl_texture tex = lookup(res, res_count, cmd.resource_key);
            if (tex != 0) {
                float u0 = cmd.uv_rect.x;
                float v0 = cmd.uv_rect.y;
                float u1 = u0 + ((cmd.uv_rect.width  > 0.0f) ? cmd.uv_rect.width  : 1.0f);
                float v1 = v0 + ((cmd.uv_rect.height > 0.0f) ? cmd.uv_rect.height : 1.0f);
                zcsr_gl_queue_atlas_sprite(renderer, tex, x, y, w, h,
                                           u0, v0, u1, v1, cr, cg, cb, ca);
            } else {
                zcsr_gl_fill_rect(renderer, x, y, w, h, 0.35f, 0.10f, 0.42f, 0.85f);
                zcsr_gl_draw_rect(renderer, x, y, w, h, 1.0f, 0.85f, 0.20f, 0.90f, 1.0f);
            }
            break;
        }

        case ZCSR_DRAW_CLIP_PUSH: {
            int nx = (int)x, ny = (int)y, nw = (int)w, nh = (int)h;
            if (clip_depth > 0) {
                int px = clip_x[clip_depth - 1], py = clip_y[clip_depth - 1];
                int pw = clip_w[clip_depth - 1], ph = clip_h[clip_depth - 1];
                int right  = (nx + nw < px + pw) ? nx + nw : px + pw;
                int bottom = (ny + nh < py + ph) ? ny + nh : py + ph;
                if (nx < px) nx = px;
                if (ny < py) ny = py;
                nw = right - nx;
                nh = bottom - ny;
            }
            if (clip_depth < ZCSR_UI_CLIP_STACK_DEPTH) {
                clip_x[clip_depth] = nx;
                clip_y[clip_depth] = ny;
                clip_w[clip_depth] = nw;
                clip_h[clip_depth] = nh;
                clip_depth++;
            }
            zcsr_gl_scissor(renderer, nx, ny, nw, nh);
            break;
        }

        case ZCSR_DRAW_CLIP_POP:
            if (clip_depth > 0) clip_depth--;
            if (clip_depth > 0) {
                zcsr_gl_scissor(renderer, clip_x[clip_depth - 1], clip_y[clip_depth - 1],
                                clip_w[clip_depth - 1], clip_h[clip_depth - 1]);
            } else {
                zcsr_gl_scissor_off(renderer);
            }
            break;

        default:
            break;
        }
    }

    /* 绘制结束必须解除裁剪，否则会污染宿主后续的其它渲染 */
    if (clip_depth > 0) zcsr_gl_scissor_off(renderer);
}
