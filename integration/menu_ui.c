/* integration/menu_ui.c — 通用菜单 UI 组件实现. */
#include "menu_ui.h"
#include <string.h>
#include <stdlib.h>

/* ---- 内部结构体 ---- */
struct zcsr_menu {
    const char*  title;
    const zcsr_menu_item* items;
    int          item_count;
    int          x;
    int          y;
    int          item_height;
    float        text_scale;

    float title_r, title_g, title_b;
    float item_r,  item_g,  item_b;
    float sel_r,   sel_g,   sel_b;
    float sel_bg_r, sel_bg_g, sel_bg_b;
    float disabled_r, disabled_g, disabled_b;

    int selected;  /* 当前选中索引, -1 = 无有效项 */
};

/* ---- 辅助: 找下一个非禁用项 ---- */
static int find_next_enabled(const zcsr_menu* m, int from, int dir) {
    if (m->item_count <= 0) return -1;
    int idx = from;
    for (int i = 0; i < m->item_count; i++) {
        idx += dir;
        if (idx < 0) idx = m->item_count - 1;
        if (idx >= m->item_count) idx = 0;
        if (!m->items[idx].disabled) return idx;
    }
    return -1;  /* 全部禁用 */
}

static int first_enabled(const zcsr_menu* m) {
    for (int i = 0; i < m->item_count; i++) {
        if (!m->items[i].disabled) return i;
    }
    return -1;
}

/* ---- 颜色默认值 ---- */
static float def(float v, float d) { return (v == 0.0f) ? d : v; }

/* ---- 创建 ---- */
zcsr_menu* zcsr_menu_create(void* buf, size_t bytes, const zcsr_menu_config* cfg) {
    if (!buf || !cfg || bytes < sizeof(zcsr_menu)) return NULL;
    if (!cfg->items || cfg->item_count <= 0) return NULL;

    zcsr_menu* m = (zcsr_menu*)buf;
    memset(m, 0, sizeof(*m));

    m->title       = cfg->title;
    m->items       = cfg->items;
    m->item_count  = cfg->item_count;
    m->x           = cfg->x;
    m->y           = cfg->y;
    m->item_height = (cfg->item_height > 0) ? cfg->item_height : 36;
    m->text_scale  = (cfg->text_scale > 0.0f) ? cfg->text_scale : 2.0f;

    m->title_r    = def(cfg->title_r,    1.0f); m->title_g = def(cfg->title_g, 0.9f); m->title_b = def(cfg->title_b, 0.5f);
    m->item_r     = def(cfg->item_r,     0.8f); m->item_g  = def(cfg->item_g,  0.8f); m->item_b  = def(cfg->item_b,  0.8f);
    m->sel_r      = def(cfg->sel_r,      1.0f); m->sel_g   = def(cfg->sel_g,   1.0f); m->sel_b   = def(cfg->sel_b,   1.0f);
    m->sel_bg_r   = def(cfg->sel_bg_r,   0.2f); m->sel_bg_g= def(cfg->sel_bg_g,0.4f); m->sel_bg_b= def(cfg->sel_bg_b, 0.8f);
    m->disabled_r = def(cfg->disabled_r, 0.4f); m->disabled_g=def(cfg->disabled_g,0.4f);m->disabled_b=def(cfg->disabled_b,0.4f);

    m->selected = first_enabled(m);
    return m;
}

void zcsr_menu_destroy(zcsr_menu* menu) {
    (void)menu; /* 无堆分配, 预留 */
}

/* ---- 按键处理 ---- */
int zcsr_menu_handle_key(zcsr_menu* menu, zcsr_keycode key, zcsr_key_action action) {
    if (!menu || action != ZCSR_KEY_DOWN) return -1;

    switch (key) {
        /* 上: Up arrow (38), W (87) */
        case 38:
        case 87: {
            int next = find_next_enabled(menu, menu->selected, -1);
            if (next >= 0) menu->selected = next;
            return -1;
        }
        /* 下: Down arrow (40), S (83) */
        case 40:
        case 83: {
            int next = find_next_enabled(menu, menu->selected, 1);
            if (next >= 0) menu->selected = next;
            return -1;
        }
        /* 确认: Enter (13), Space (32) */
        case 13:
        case 32: {
            if (menu->selected >= 0 && menu->selected < menu->item_count) {
                if (!menu->items[menu->selected].disabled) {
                    return menu->items[menu->selected].id;
                }
            }
            return -1;
        }
        /* 取消: ESC (27) */
        case 27:
            return -2;

        default:
            return -1;
    }
}

/* ---- 渲染 ---- */
void zcsr_menu_render(zcsr_menu* menu, zcsr_gl_renderer* renderer) {
    if (!menu || !renderer) return;

    float cy = (float)menu->y;
    const float pad_x = 16.0f;

    /* 标题 */
    if (menu->title && menu->title[0]) {
        zcsr_gl_draw_text(renderer, menu->title,
                          (float)menu->x, cy, menu->text_scale,
                          menu->title_r, menu->title_g, menu->title_b, 1.0f);
        cy += (float)(menu->item_height + 8);
    }

    /* 菜单项 */
    for (int i = 0; i < menu->item_count; i++) {
        const zcsr_menu_item* item = &menu->items[i];
        bool is_sel = (i == menu->selected);

        /* 选中项背景 */
        if (is_sel) {
            float bg_w = 280.0f; /* 固定宽度背景, 可后续改为自适应 */
            zcsr_gl_fill_rect(renderer,
                              (float)menu->x - 8.0f, cy - 2.0f,
                              bg_w, (float)menu->item_height - 4.0f,
                              menu->sel_bg_r, menu->sel_bg_g, menu->sel_bg_b, 0.7f);
        }

        /* 文字 */
        float r, g, b;
        if (item->disabled) {
            r = menu->disabled_r; g = menu->disabled_g; b = menu->disabled_b;
        } else if (is_sel) {
            r = menu->sel_r; g = menu->sel_g; b = menu->sel_b;
        } else {
            r = menu->item_r; g = menu->item_g; b = menu->item_b;
        }

        /* 选中项前面加箭头 */
        if (is_sel) {
            zcsr_gl_draw_text(renderer, ">",
                              (float)menu->x, cy, menu->text_scale,
                              r, g, b, 1.0f);
        }

        zcsr_gl_draw_text(renderer, item->label,
                          (float)menu->x + pad_x + (is_sel ? 0.0f : 0.0f),
                          cy, menu->text_scale,
                          r, g, b, 1.0f);

        cy += (float)menu->item_height;
    }
}

/* ---- 访问器 ---- */
int zcsr_menu_get_selected_index(const zcsr_menu* menu) {
    return menu ? menu->selected : -1;
}

int zcsr_menu_get_selected_id(const zcsr_menu* menu) {
    if (!menu || menu->selected < 0 || menu->selected >= menu->item_count) return -1;
    return menu->items[menu->selected].id;
}

void zcsr_menu_reset(zcsr_menu* menu) {
    if (menu) menu->selected = first_enabled(menu);
}

void zcsr_menu_set_selected(zcsr_menu* menu, int index) {
    if (!menu) return;
    if (index < 0 || index >= menu->item_count) return;
    if (menu->items[index].disabled) return;
    menu->selected = index;
}
