/* modules/ui/ui_gl_backend.c — OpenGL rendering backend for ZCSR UI.
 *
 * Translates backend-neutral draw commands into gl_render primitives.
 * Implements the interface declared in include/zcsr/ui_gl_backend.h.
 */
#include "zcsr/ui_gl_backend.h"
#include "zcsr/ui_draw.h"
#include "zcsr/ui_types.h"

#include <string.h>

/* ---- Colors (modern dark theme) ---- */
static const float COL_BG[4]        = { 0.08f, 0.09f, 0.11f, 1.0f };
static const float COL_PANEL[4]     = { 0.12f, 0.13f, 0.16f, 1.0f };
static const float COL_BORDER[4]    = { 0.25f, 0.26f, 0.30f, 1.0f };
static const float COL_TEXT[4]      = { 0.90f, 0.90f, 0.92f, 1.0f };
static const float COL_TEXT_DIM[4]  = { 0.50f, 0.52f, 0.55f, 1.0f };
static const float COL_ACCENT[4]    = { 0.20f, 0.55f, 0.95f, 1.0f };
static const float COL_HOVER[4]     = { 0.18f, 0.20f, 0.24f, 1.0f };
static const float COL_PRESSED[4]  = { 0.15f, 0.17f, 0.21f, 1.0f };

static zcsr_font_atlas* g_font_atlas = NULL;

void zcsr_ui_gl_set_font_atlas(zcsr_font_atlas* atlas) {
    g_font_atlas = atlas;
}

/* ---- Helper: pick widget color based on state ---- */
static void pick_widget_color(const zcsr_ui_draw_command* cmd, float* r, float* g, float* b, float* a) {
    if (!cmd->enabled) {
        *r = COL_BORDER[0]; *g = COL_BORDER[1]; *b = COL_BORDER[2]; *a = 0.5f;
        return;
    }
    if (cmd->pressed) {
        *r = COL_PRESSED[0]; *g = COL_PRESSED[1]; *b = COL_PRESSED[2]; *a = 1.0f;
    } else if (cmd->hovered) {
        *r = COL_HOVER[0]; *g = COL_HOVER[1]; *b = COL_HOVER[2]; *a = 1.0f;
    } else {
        *r = COL_PANEL[0]; *g = COL_PANEL[1]; *b = COL_PANEL[2]; *a = 1.0f;
    }
}

/* ---- Render one draw command ---- */
static zcsr_gl_texture lookup_resource(const zcsr_ui_gl_resource* res, size_t count,
                                      zcsr_ui_key key) {
    if (res == NULL || key == 0) return 0;
    for (size_t i = 0; i < count; ++i) {
        if (res[i].resource_key == key) return res[i].texture;
    }
    return 0;
}

static void render_command(zcsr_gl_renderer* r, const zcsr_ui_draw_command* cmd,
                           const zcsr_ui_gl_resource* res, size_t res_count) {
    float x = cmd->rect.x;
    float y = cmd->rect.y;
    float w = cmd->rect.w;
    float h = cmd->rect.h;

    switch (cmd->kind) {
    case ZCSR_UI_DRAW_WIDGET: {
        float cr, cg, cb, ca;
        pick_widget_color(cmd, &cr, &cg, &cb, &ca);

        switch (cmd->widget_type) {
        case ZCSR_UI_PANEL:
            /* Panel: filled rect + border */
            zcsr_gl_fill_rect(r, x, y, w, h, cr, cg, cb, ca);
            zcsr_gl_draw_rect(r, x, y, w, h, 1.0f, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2], 1.0f);
            break;

        case ZCSR_UI_BUTTON:
            /* Button: filled rect + border */
            zcsr_gl_fill_rect(r, x, y, w, h, cr, cg, cb, ca);
            zcsr_gl_draw_rect(r, x, y, w, h, 1.0f, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2], 1.0f);
            break;

        case ZCSR_UI_TOGGLE:
            /* Toggle: track + knob */
            zcsr_gl_fill_rect(r, x, y, w, h, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2], 1.0f);
            if (cmd->selected) {
                /* On: knob right side, accent color */
                float knob_w = h;
                zcsr_gl_fill_rect(r, x + w - knob_w, y, knob_w, h, COL_ACCENT[0], COL_ACCENT[1], COL_ACCENT[2], 1.0f);
            } else {
                /* Off: knob left side */
                float knob_w = h;
                zcsr_gl_fill_rect(r, x, y, knob_w, h, COL_TEXT_DIM[0], COL_TEXT_DIM[1], COL_TEXT_DIM[2], 1.0f);
            }
            break;

        case ZCSR_UI_SLIDER: {
            /* Slider: background track + filled portion + knob */
            zcsr_gl_fill_rect(r, x, y + h*0.5f - 2.0f, w, 4.0f, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2], 1.0f);

            float t = 0.0f;
            if (cmd->maximum > cmd->minimum) {
                t = (cmd->value.as.real - cmd->minimum) / (cmd->maximum - cmd->minimum);
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
            }
            zcsr_gl_fill_rect(r, x, y + h*0.5f - 2.0f, w * t, 4.0f, COL_ACCENT[0], COL_ACCENT[1], COL_ACCENT[2], 1.0f);

            /* Knob */
            float knob_x = x + w * t - 6.0f;
            zcsr_gl_fill_rect(r, knob_x, y + h*0.5f - 6.0f, 12.0f, 12.0f, COL_TEXT[0], COL_TEXT[1], COL_TEXT[2], 1.0f);
            break;
        }

        case ZCSR_UI_DROPDOWN:
            /* Dropdown: filled rect + border + arrow */
            zcsr_gl_fill_rect(r, x, y, w, h, cr, cg, cb, ca);
            zcsr_gl_draw_rect(r, x, y, w, h, 1.0f, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2], 1.0f);
            /* Down arrow */
            float ax = x + w - 12.0f;
            float ay = y + h * 0.5f;
            zcsr_gl_draw_line(r, ax, ay - 2.0f, ax + 4.0f, ay + 2.0f, 1.5f, COL_TEXT[0], COL_TEXT[1], COL_TEXT[2], 1.0f);
            zcsr_gl_draw_line(r, ax + 4.0f, ay + 2.0f, ax + 8.0f, ay - 2.0f, 1.5f, COL_TEXT[0], COL_TEXT[1], COL_TEXT[2], 1.0f);
            break;

        case ZCSR_UI_LABEL:
            /* Label: no background, just text (drawn separately) */
            break;

        case ZCSR_UI_SEPARATOR:
            /* Separator: horizontal line */
            zcsr_gl_fill_rect(r, x, y, w, 1.0f, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2], 1.0f);
            break;

        case ZCSR_UI_IMAGE: {
            /* Image: 用宿主给的 resource_key -> texture 表真画纹理。
             * 找不到资源时画洋红半透明 + 边框，这是"缺资源"的明确可视信号，
             * 不再用灰块假装有图。 */
            zcsr_gl_texture tex = lookup_resource(res, res_count, cmd->resource_key);
            if (tex != 0) {
                float u0 = cmd->uv_rect.x;
                float v0 = cmd->uv_rect.y;
                float u1 = u0 + ((cmd->uv_rect.w > 0.0f) ? cmd->uv_rect.w : 1.0f);
                float v1 = v0 + ((cmd->uv_rect.h > 0.0f) ? cmd->uv_rect.h : 1.0f);
                zcsr_gl_queue_atlas_sprite(r, tex, x, y, w, h,
                                           u0, v0, u1, v1,
                                           cr, cg, cb, 1.0f);
            } else {
                zcsr_gl_fill_rect(r, x, y, w, h, 0.35f, 0.10f, 0.42f, 0.85f);
                zcsr_gl_draw_rect(r, x, y, w, h, 1.0f, 0.85f, 0.20f, 0.90f, 1.0f);
            }
            break;
        }

        case ZCSR_UI_TEXTINPUT:
            /* TextInput: filled rect + border */
            zcsr_gl_fill_rect(r, x, y, w, h, 0.05f, 0.06f, 0.08f, 1.0f);
            zcsr_gl_draw_rect(r, x, y, w, h, 1.0f, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2], 1.0f);
            break;

        case ZCSR_UI_SCROLLVIEW:
            /* ScrollView: filled rect + border */
            zcsr_gl_fill_rect(r, x, y, w, h, 0.05f, 0.06f, 0.08f, 1.0f);
            zcsr_gl_draw_rect(r, x, y, w, h, 1.0f, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2], 1.0f);
            break;

        default:
            break;
        }
        break;
    }

    case ZCSR_UI_DRAW_LABEL:
        /* Label text */
        if (cmd->text && cmd->text_length > 0) {
            zcsr_gl_draw_text(r, cmd->text, x, y, 1.0f, COL_TEXT[0], COL_TEXT[1], COL_TEXT[2], 1.0f);
        }
        break;

    case ZCSR_UI_DRAW_POPUP:
        /* Popup background */
        zcsr_gl_fill_rect(r, x, y, w, h, COL_PANEL[0], COL_PANEL[1], COL_PANEL[2], 0.98f);
        zcsr_gl_draw_rect(r, x, y, w, h, 1.0f, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2], 1.0f);
        break;

    case ZCSR_UI_DRAW_POPUP_OPTION:
        /* Popup option row */
        if (cmd->hovered) {
            zcsr_gl_fill_rect(r, x, y, w, h, COL_HOVER[0], COL_HOVER[1], COL_HOVER[2], 1.0f);
        }
        if (cmd->text && cmd->text_length > 0) {
            zcsr_gl_draw_text(r, cmd->text, x + 8.0f, y + 4.0f, 1.0f, COL_TEXT[0], COL_TEXT[1], COL_TEXT[2], 1.0f);
        }
        break;

    default:
        break;
    }
}

void zcsr_ui_gl_render(zcsr_gl_renderer* renderer,
                       const zcsr_ui_draw_list* list,
                       const zcsr_ui_gl_resource* resources,
                       size_t resource_count) {
    if (!renderer || !list) return;

    for (size_t i = 0; i < list->count; i++) {
        render_command(renderer, &list->items[i], resources, resource_count);
    }
}
