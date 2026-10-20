/* integration/settings_ui.c — 游戏设置面板骨架实现. */
#include "settings_ui.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ---- 内部结构体 ---- */
struct zcsr_settings {
    const char*  title;
    zcsr_setting_item* items;
    int    count;
    int    x, y;
    int    width;
    int    item_height;
    float  text_scale;
    int    selected;     /* 当前选中项索引 (跳过 separator) */
    int    button;       /* 0=apply, 1=cancel, 2=reset, -1=none (focus on items) */
    bool   dirty;
};

/* ---- 辅助: 找下一个非 separator 项 ---- */
static int find_next_item(const zcsr_settings* s, int from, int dir) {
    if (s->count <= 0) return -1;
    int idx = from;
    for (int i = 0; i < s->count; i++) {
        idx += dir;
        if (idx < 0) idx = s->count - 1;
        if (idx >= s->count) idx = 0;
        if (s->items[idx].type != ZCSR_CTRL_SEPARATOR) return idx;
    }
    return -1;
}

static int first_item(const zcsr_settings* s) {
    for (int i = 0; i < s->count; i++) {
        if (s->items[i].type != ZCSR_CTRL_SEPARATOR) return i;
    }
    return -1;
}

/* ---- 检查 dirty ---- */
static void recompute_dirty(zcsr_settings* s) {
    s->dirty = false;
    for (int i = 0; i < s->count; i++) {
        zcsr_setting_item* it = &s->items[i];
        if (it->type == ZCSR_CTRL_DROPDOWN && it->pending_value != it->value) { s->dirty = true; return; }
        if (it->type == ZCSR_CTRL_SLIDER && fabsf(it->pending_slider - it->slider_value) > 0.001f) { s->dirty = true; return; }
    }
}

/* ---- 创建 ---- */
zcsr_settings* zcsr_settings_create(void* buf, size_t bytes, const zcsr_settings_config* cfg) {
    if (!buf || !cfg || bytes < sizeof(zcsr_settings)) return NULL;
    if (!cfg->items || cfg->count <= 0) return NULL;

    zcsr_settings* s = (zcsr_settings*)buf;
    memset(s, 0, sizeof(*s));
    s->title       = cfg->title;
    s->items       = cfg->items;
    s->count       = cfg->count;
    s->x           = cfg->x;
    s->y           = cfg->y;
    s->width       = (cfg->width > 0) ? cfg->width : 500;
    s->item_height = (cfg->item_height > 0) ? cfg->item_height : 34;
    s->text_scale  = (cfg->text_scale > 0.0f) ? cfg->text_scale : 1.5f;
    s->button      = -1;

    /* 初始化暂存值 */
    for (int i = 0; i < s->count; i++) {
        s->items[i].pending_value  = s->items[i].value;
        s->items[i].pending_slider = s->items[i].slider_value;
    }

    s->selected = first_item(s);
    s->dirty = false;
    return s;
}

void zcsr_settings_destroy(zcsr_settings* s) { (void)s; }

/* ---- 按键处理 ---- */
int zcsr_settings_handle_key(zcsr_settings* s, zcsr_keycode key, zcsr_key_action action) {
    if (!s || action != ZCSR_KEY_DOWN) return 0;

    /* 底部按钮模式: Left/Right 切换按钮, Enter 确认 */
    if (s->button >= 0) {
        switch (key) {
            case 37: case 65: /* Left / A */
                s->button = (s->button + 2) % 3; return 0;
            case 39: case 68: /* Right / D */
                s->button = (s->button + 1) % 3; return 0;
            case 13: case 32: /* Enter / Space */
                if (s->button == 0) { zcsr_settings_apply(s); return 1; }
                if (s->button == 1) { zcsr_settings_cancel(s); return -1; }
                if (s->button == 2) { zcsr_settings_reset(s); return 0; }
                return 0;
            case 27: /* ESC -> cancel */
                zcsr_settings_cancel(s); return -1;
            case 38: case 87: /* Up / W -> back to items */
                s->button = -1; return 0;
        }
        return 0;
    }

    /* 设置项模式 */
    switch (key) {
        /* 下: Down (40), S (83) */
        case 40: case 83: {
            int next = find_next_item(s, s->selected, 1);
            if (next >= 0) s->selected = next;
            return 0;
        }
        /* 上: Up (38), W (87) */
        case 38: case 87: {
            int prev = find_next_item(s, s->selected, -1);
            if (prev >= 0) s->selected = prev;
            return 0;
        }
        /* 右: Right (39), D (68) — 增加/下一个选项 */
        case 39: case 68: {
            if (s->selected < 0) return 0;
            zcsr_setting_item* it = &s->items[s->selected];
            if (it->type == ZCSR_CTRL_DROPDOWN) {
                if (it->pending_value < it->option_count - 1) it->pending_value++;
            } else if (it->type == ZCSR_CTRL_SLIDER) {
                it->pending_slider += it->step;
                if (it->pending_slider > it->max) it->pending_slider = it->max;
            }
            recompute_dirty(s);
            return 0;
        }
        /* 左: Left (37), A (65) — 减少/上一个选项 */
        case 37: case 65: {
            if (s->selected < 0) return 0;
            zcsr_setting_item* it = &s->items[s->selected];
            if (it->type == ZCSR_CTRL_DROPDOWN) {
                if (it->pending_value > 0) it->pending_value--;
            } else if (it->type == ZCSR_CTRL_SLIDER) {
                it->pending_slider -= it->step;
                if (it->pending_slider < it->min) it->pending_slider = it->min;
            }
            recompute_dirty(s);
            return 0;
        }
        /* Tab -> 跳到底部按钮 */
        case 9:
            s->button = 0; return 0;
        /* Enter -> apply */
        case 13:
            zcsr_settings_apply(s); return 1;
        /* ESC -> cancel */
        case 27:
            zcsr_settings_cancel(s); return -1;
    }
    return 0;
}

/* ---- 渲染 ---- */
void zcsr_settings_render(zcsr_settings* s, zcsr_gl_renderer* r) {
    if (!s || !r) return;

    float cy = (float)s->y;
    const float label_x = (float)s->x;
    const float ctrl_right = (float)(s->x + s->width);
    const float scale = s->text_scale;
    const float row_h = (float)s->item_height;

    /* 面板背景 */
    float panel_h = (float)(s->count * s->item_height) + (s->title ? row_h + 10 : 0) + row_h * 1.5f;
    zcsr_gl_fill_rect(r, (float)s->x - 16, (float)s->y - 16,
                      (float)s->width + 32, panel_h + 16,
                      0.06f, 0.07f, 0.10f, 0.92f);

    /* 标题 */
    if (s->title && s->title[0]) {
        zcsr_gl_draw_text(r, s->title, label_x, cy, scale * 1.3f,
                          0.4f, 0.7f, 1.0f, 1.0f);
        cy += row_h + 8;
        /* 标题下划线 */
        zcsr_gl_fill_rect(r, label_x, cy, (float)s->width, 1.0f, 0.2f, 0.3f, 0.5f, 0.8f);
        cy += 6;
    }

    /* 设置项 */
    for (int i = 0; i < s->count; i++) {
        zcsr_setting_item* it = &s->items[i];
        bool is_sel = (i == s->selected && s->button < 0);

        if (it->type == ZCSR_CTRL_SEPARATOR) {
            /* 分组标题 */
            zcsr_gl_draw_text(r, it->label, label_x, cy + 4, scale * 0.85f,
                              0.5f, 0.5f, 0.6f, 1.0f);
            cy += row_h * 0.7f;
            continue;
        }

        /* 选中项高亮背景 */
        if (is_sel) {
            zcsr_gl_fill_rect(r, label_x - 8, cy - 2,
                              (float)s->width + 16, row_h - 4,
                              0.15f, 0.25f, 0.5f, 0.6f);
        }

        /* 标签 */
        float lr, lg, lb;
        if (is_sel) { lr = 1.0f; lg = 1.0f; lb = 1.0f; }
        else { lr = 0.75f; lg = 0.78f; lb = 0.82f; }
        zcsr_gl_draw_text(r, it->label, label_x, cy, scale, lr, lg, lb, 1.0f);

        /* 控件 (右对齐) */
        if (it->type == ZCSR_CTRL_DROPDOWN) {
            const char* val = (it->pending_value >= 0 && it->pending_value < it->option_count)
                              ? it->options[it->pending_value] : "?";
            /* 左右箭头 */
            char buf[64];
            snprintf(buf, sizeof(buf), "< %s >", val);
            float text_w = (float)strlen(buf) * 8.0f * scale;
            zcsr_gl_draw_text(r, buf, ctrl_right - text_w, cy, scale,
                              is_sel ? 1.0f : 0.6f, is_sel ? 1.0f : 0.8f,
                              is_sel ? 0.6f : 0.5f, 1.0f);
            /* 修改标记 */
            if (it->pending_value != it->value) {
                zcsr_gl_draw_text(r, "*", ctrl_right + 6, cy, scale, 1.0f, 0.8f, 0.2f, 1.0f);
            }
        } else if (it->type == ZCSR_CTRL_SLIDER) {
            /* 滑块轨道 */
            const float slider_w = 140.0f;
            const float slider_x = ctrl_right - slider_w;
            const float slider_y = cy + row_h * 0.35f;
            const float slider_h = 6.0f;
            zcsr_gl_fill_rect(r, slider_x, slider_y, slider_w, slider_h,
                              0.15f, 0.15f, 0.2f, 1.0f);
            /* 填充 */
            float ratio = (it->max > it->min) ? (it->pending_slider - it->min) / (it->max - it->min) : 0.0f;
            if (ratio < 0) ratio = 0; if (ratio > 1) ratio = 1;
            zcsr_gl_fill_rect(r, slider_x, slider_y, slider_w * ratio, slider_h,
                              is_sel ? 0.3f : 0.2f, is_sel ? 0.6f : 0.4f,
                              is_sel ? 1.0f : 0.7f, 1.0f);
            /* 数值 */
            char vbuf[16];
            snprintf(vbuf, sizeof(vbuf), "%.0f", it->pending_slider);
            float vw = (float)strlen(vbuf) * 8.0f * scale;
            zcsr_gl_draw_text(r, vbuf, slider_x - vw - 8, cy, scale,
                              is_sel ? 1.0f : 0.7f, is_sel ? 1.0f : 0.7f,
                              is_sel ? 0.8f : 0.6f, 1.0f);
            if (fabsf(it->pending_slider - it->slider_value) > 0.001f) {
                zcsr_gl_draw_text(r, "*", ctrl_right + 6, cy, scale, 1.0f, 0.8f, 0.2f, 1.0f);
            }
        }

        cy += row_h;
    }

    cy += 10;

    /* 底部按钮 */
    const char* btn_labels[3] = { "Apply", "Cancel", "Reset" };
    float btn_x = label_x;
    for (int b = 0; b < 3; b++) {
        bool bsel = (s->button == b);
        float bw = 80.0f;
        if (bsel) {
            zcsr_gl_fill_rect(r, btn_x - 6, cy - 2, bw + 12, row_h - 4,
                              0.2f, 0.5f, 0.9f, 0.7f);
        }
        zcsr_gl_draw_text(r, btn_labels[b], btn_x, cy, scale,
                          bsel ? 1.0f : 0.6f, bsel ? 1.0f : 0.6f,
                          bsel ? 1.0f : 0.6f, 1.0f);
        btn_x += bw + 20;
    }

    /* dirty 提示 */
    if (s->dirty) {
        zcsr_gl_draw_text(r, "* unsaved changes", ctrl_right - 160, cy, scale * 0.8f,
                          1.0f, 0.7f, 0.2f, 1.0f);
    }
}

/* ---- apply / cancel / reset ---- */
void zcsr_settings_apply(zcsr_settings* s) {
    if (!s) return;
    for (int i = 0; i < s->count; i++) {
        s->items[i].value        = s->items[i].pending_value;
        s->items[i].slider_value = s->items[i].pending_slider;
    }
    s->dirty = false;
}

void zcsr_settings_cancel(zcsr_settings* s) {
    if (!s) return;
    for (int i = 0; i < s->count; i++) {
        s->items[i].pending_value  = s->items[i].value;
        s->items[i].pending_slider = s->items[i].slider_value;
    }
    s->dirty = false;
}

void zcsr_settings_reset(zcsr_settings* s) {
    if (!s) return;
    /* 调用者应先把 items[].value/slider_value 设为默认值 */
    for (int i = 0; i < s->count; i++) {
        s->items[i].pending_value  = s->items[i].value;
        s->items[i].pending_slider = s->items[i].slider_value;
    }
    s->dirty = false;
}

bool zcsr_settings_is_dirty(const zcsr_settings* s) {
    return s ? s->dirty : false;
}

int zcsr_settings_get_selected(const zcsr_settings* s) {
    return s ? s->selected : -1;
}
