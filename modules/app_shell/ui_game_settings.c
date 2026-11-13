/* modules/ui/ui_game_settings.c — 现代游戏设置面板实现.
 *
 * 特性:
 *   - 顶部标签页 (鼠标点击 / 键盘切换)
 *   - 真正可展开下拉框 (点击展开选项列表, 点击选项选中, 点击外部关闭)
 *   - 可拖拽滑块 (鼠标点击轨道跳转 / 拖拽手柄 / 键盘方向键)
 *   - 开关控件 (On/Off, 鼠标点击 / 键盘切换)
 *   - 分组分隔线
 *   - 右侧信息面板 (显示当前选中/悬停项的标题与说明)
 *   - 底部按钮 (Apply / Cancel / Reset)
 *   - 鼠标悬停高亮 + 键盘导航双模式
 *   - 修改暂存 + Apply/Cancel/Reset 语义
 */
#include "zcsr/ui_game_settings.h"
#include "zcsr/ui.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ---- 组件树上限 ---- */
#define MS_MAX_TABS  8
#define MS_MAX_ITEMS 128

/* ---- 布局常量 ---- */
#define MS_TAB_BAR_H     44
#define MS_BOTTOM_H      64
#define MS_RIGHT_PANEL_W 300
#define MS_PAD           16
#define MS_CONTROL_W     220
#define MS_SLIDER_W      200
#define MS_TOGGLE_W      60
#define MS_DD_OPT_H      28
#define MS_BTN_W         140
#define MS_BTN_H         36

/* ---- 内部矩形 ---- */
typedef struct { float x, y, w, h; } ms_rect;

/* ---- 内部结构体 ---- */
struct zcsr_game_settings {
    /* config */
    zcsr_game_settings_tab* tabs;
    int    tab_count;
    int    x, y, width, height;
    int    item_height;
    float  text_scale;

    /* computed layout */
    int content_left, content_top, content_w, content_h;
    int right_x, right_y, right_w, right_h;
    int bottom_y;

    /* navigation state */
    int active_tab;       /* current tab index */
    int selected_item;    /* keyboard focus item (-1 = none) */
    int hovered_tab;      /* mouse hover tab (-1 = none) */
    int hovered_item;     /* mouse hover item (-1 = none) */
    int hovered_button;   /* mouse hover button (-1 = none) */
    int focus_area;       /* 0 = items, 1 = buttons */
    int selected_button;  /* keyboard focus button (0=apply,1=cancel,2=reset) */

    /* dropdown state */
    int dropdown_open;    /* item index or -1 */
    int dropdown_hover_opt;
    bool dropdown_up;     /* true = popup expands upward, false = downward */

    /* slider drag */
    int slider_drag;      /* item index or -1 */

    /* mouse edge tracking */
    uint8_t prev_buttons;

    /* scrolling */
    int scroll_offset;  /* pixels scrolled down from top */

    bool dirty;

    /* ---- 组件树 ----
     * 每个标签页 / 每个设置项 / 每个底部按钮都是一个独立 node，位置与尺寸存在
     * node 的 local_rect 里，而不是散落在绘制代码里的硬编码偏移。布局每帧把
     * 计算结果写进 node，所有读取（命中、绘制、外部消费者）一律走 node。
     * 这样页面成为一个可枚举、可逐控件改位置的组件集合。 */
    ZCSR_UI*    ui;
    ZCSR_NodeId tab_node[MS_MAX_TABS];
    ZCSR_NodeId item_node[MS_MAX_ITEMS];
    int         item_node_base[MS_MAX_TABS]; /* 每个标签页在 item_node 里的起点 */
    int         item_node_count;
    ZCSR_NodeId btn_node[3];
    ZCSR_NodeId root_node;

    /* 外部几何覆盖（编辑器接管某个控件的 node 矩形后，公式布局不再覆盖它）。 */
    uint8_t tab_override[MS_MAX_TABS];
    uint8_t item_override[MS_MAX_ITEMS];
    uint8_t btn_override[3];

    /* 组件颜色覆盖（0 = 用主题色）。颜色属于组件，编辑器只改它、存它。 */
    uint8_t item_color_set[MS_MAX_ITEMS];
    float   item_color[MS_MAX_ITEMS][3];
    uint8_t btn_color_set[3];
    float   btn_color[3][3];
};

/* ---- 颜色 ---- */
#define C_BG_PANEL     0.05f,0.07f,0.10f,0.96f
#define C_BG_TABBAR    0.03f,0.04f,0.07f,1.0f
#define C_BG_TAB_ACT   0.10f,0.16f,0.28f,1.0f
#define C_BG_TAB_HOV   0.08f,0.12f,0.20f,1.0f
#define C_BG_CONTENT   0.04f,0.05f,0.08f,1.0f
#define C_BG_ROW_HOV   0.10f,0.14f,0.22f,0.45f
#define C_BG_ROW_SEL   0.14f,0.20f,0.36f,0.55f
#define C_BG_CTRL      0.08f,0.11f,0.16f,1.0f
#define C_BG_CTRL_HOV  0.12f,0.16f,0.24f,1.0f
#define C_BORDER       0.22f,0.28f,0.38f,1.0f
#define C_BORDER_HOV   0.38f,0.52f,0.78f,1.0f
#define C_ACCENT       0.25f,0.55f,0.95f,1.0f
#define C_ACCENT_DIM   0.14f,0.32f,0.60f,1.0f
#define C_ACCENT_GLOW  0.35f,0.65f,1.00f,1.0f
#define C_TXT_NORM     0.72f,0.76f,0.82f,1.0f
#define C_TXT_BRIGHT   1.00f,1.00f,1.00f,1.0f
#define C_TXT_DIM      0.42f,0.46f,0.52f,1.0f
#define C_TXT_ACCENT   0.40f,0.68f,1.00f,1.0f
#define C_DIRTY        1.00f,0.78f,0.20f,1.0f
#define C_TOGGLE_ON    0.20f,0.60f,0.35f,1.0f
#define C_TOGGLE_OFF   0.25f,0.25f,0.30f,1.0f

/* Safe ternary color picker: expands to 4 per-channel ternaries.
 * Usage: MS_RGBA_IF(cond, r1,g1,b1,a1, r2,g2,b2,a2) — each value must be a single float. */
#define MS_RGBA_IF(c, r1,g1,b1,a1, r2,g2,b2,a2) \
    ((c)?(r1):(r2)), ((c)?(g1):(g2)), ((c)?(b1):(b2)), ((c)?(a1):(a2))

/* ---- 辅助 ---- */
static inline bool point_in_rect(int px, int py, ms_rect r) {
    return (float)px >= r.x && (float)px < r.x + r.w &&
           (float)py >= r.y && (float)py < r.y + r.h;
}

static inline float text_w(const char* s, float scale) {
    return (float)strlen(s) * 8.0f * scale;
}

static inline zcsr_game_settings_tab* cur_tab(zcsr_game_settings* s) {
    return &s->tabs[s->active_tab];
}

static int first_selectable(zcsr_game_settings* s) {
    zcsr_game_settings_tab* t = cur_tab(s);
    for (int i = 0; i < t->count; i++)
        if (t->items[i].type != ZCSR_GAME_SETTINGS_SEPARATOR) return i;
    return -1;
}

static int find_next_item(zcsr_game_settings* s, int from, int dir) {
    zcsr_game_settings_tab* t = cur_tab(s);
    if (t->count <= 0) return -1;
    int idx = from;
    for (int i = 0; i < t->count; i++) {
        idx += dir;
        if (idx < 0) idx = t->count - 1;
        if (idx >= t->count) idx = 0;
        if (t->items[idx].type != ZCSR_GAME_SETTINGS_SEPARATOR) return idx;
    }
    return -1;
}

static void recompute_dirty(zcsr_game_settings* s) {
    s->dirty = false;
    for (int t = 0; t < s->tab_count; t++) {
        zcsr_game_settings_tab* tab = &s->tabs[t];
        for (int i = 0; i < tab->count; i++) {
            zcsr_game_settings_item* it = &tab->items[i];
            if (it->type == ZCSR_GAME_SETTINGS_DROPDOWN && it->pending_value != it->value) { s->dirty = true; return; }
            if (it->type == ZCSR_GAME_SETTINGS_SLIDER && fabsf(it->pending_slider - it->slider_value) > 0.001f) { s->dirty = true; return; }
            if (it->type == ZCSR_GAME_SETTINGS_TOGGLE && it->pending_toggle != it->toggle_value) { s->dirty = true; return; }
        }
    }
}

/* ---- 布局计算 ---- */
/* node 是几何权威；公式只在 node 尚未建立时兜底。 */
static bool ms_node_rect(const zcsr_game_settings* s, ZCSR_NodeId id, ms_rect* out) {
    ZCSR_Rect r;
    if (!s->ui || (uint32_t)id == 0u) return false;
    if (zcsr_node_get_rect(s->ui, id, &r) != ZCSR_OK) return false;
    *out = (ms_rect){ r.x, r.y, r.width, r.height };
    return true;
}

static ms_rect tab_rect(const zcsr_game_settings* s, int idx) {
    ms_rect n;
    if (idx >= 0 && idx < s->tab_count && idx < MS_MAX_TABS &&
        ms_node_rect(s, s->tab_node[idx], &n))
        return n;
    /* tabs are left-aligned in the tab bar, each tab width = text + 32 pad */
    float scale = s->text_scale;
    float tw = text_w(s->tabs[idx].name, scale) + 32.0f;
    float cx = (float)s->x + MS_PAD;
    for (int i = 0; i < idx; i++)
        cx += text_w(s->tabs[i].name, scale) + 32.0f + 4.0f;
    return (ms_rect){ cx, (float)s->y + 4.0f, tw, (float)(MS_TAB_BAR_H - 8) };
}

static ms_rect item_row_rect(const zcsr_game_settings* s, int idx) {
    ms_rect n;
    int tab = s->active_tab;
    if (tab >= 0 && tab < MS_MAX_TABS && idx >= 0) {
        int f = s->item_node_base[tab] + idx;
        if (f >= 0 && f < s->item_node_count && ms_node_rect(s, s->item_node[f], &n))
            return n;
    }
    return (ms_rect){
        (float)s->content_left,
        (float)s->content_top + (float)idx * (float)s->item_height - (float)s->scroll_offset,
        (float)s->content_w,
        (float)s->item_height
    };
}

static void clamp_scroll(zcsr_game_settings* s) {
    int total_h = cur_tab(s)->count * s->item_height;
    int max_scroll = total_h - s->content_h;
    if (max_scroll < 0) max_scroll = 0;
    if (s->scroll_offset < 0) s->scroll_offset = 0;
    if (s->scroll_offset > max_scroll) s->scroll_offset = max_scroll;
}

static void ensure_item_visible(zcsr_game_settings* s, int idx) {
    if (idx < 0) return;
    int item_top = idx * s->item_height;
    int item_bot = item_top + s->item_height;
    if (item_top < s->scroll_offset) {
        s->scroll_offset = item_top;
    } else if (item_bot > s->scroll_offset + s->content_h) {
        s->scroll_offset = item_bot - s->content_h;
    }
    clamp_scroll(s);
}

static ms_rect dropdown_closed_rect(const zcsr_game_settings* s, int idx) {
    ms_rect row = item_row_rect(s, idx);
    float cy = row.y + ((float)s->item_height - 24.0f) * 0.5f;
    return (ms_rect){ row.x + row.w - MS_CONTROL_W, cy, MS_CONTROL_W, 24.0f };
}

static ms_rect dropdown_option_rect(const zcsr_game_settings* s, int item_idx, int opt_idx) {
    ms_rect dd = dropdown_closed_rect(s, item_idx);
    if (s->dropdown_up) {
        /* expand upward: option 0 is at the bottom, closest to the closed box */
        float total_h = (float)s->tabs[s->active_tab].items[item_idx].option_count * MS_DD_OPT_H;
        return (ms_rect){ dd.x, dd.y - total_h + (float)(opt_idx + 1) * MS_DD_OPT_H - MS_DD_OPT_H, dd.w, MS_DD_OPT_H };
    }
    return (ms_rect){ dd.x, dd.y + dd.h + (float)opt_idx * MS_DD_OPT_H, dd.w, MS_DD_OPT_H };
}

/* compute whether dropdown should expand upward to stay within content area */
static bool compute_dropdown_up(const zcsr_game_settings* s, int item_idx) {
    const zcsr_game_settings_item* it = &s->tabs[s->active_tab].items[item_idx];
    ms_rect cb = dropdown_closed_rect(s, item_idx);
    float pop_h = (float)it->option_count * MS_DD_OPT_H;
    float bottom = cb.y + cb.h + pop_h;
    float content_bottom = (float)s->content_top + (float)s->content_h;
    float top = cb.y - pop_h;
    float content_top = (float)s->content_top;
    /* prefer downward; if it overflows and upward fits, go up */
    if (bottom > content_bottom && top >= content_top) return true;
    return false;
}

static ms_rect slider_track_rect(const zcsr_game_settings* s, int idx) {
    ms_rect row = item_row_rect(s, idx);
    float cy = row.y + ((float)s->item_height - 6.0f) * 0.5f;
    return (ms_rect){ row.x + row.w - MS_SLIDER_W - 40.0f, cy, MS_SLIDER_W, 6.0f };
}

static ms_rect toggle_rect(const zcsr_game_settings* s, int idx) {
    ms_rect row = item_row_rect(s, idx);
    float cy = row.y + ((float)s->item_height - 22.0f) * 0.5f;
    return (ms_rect){ row.x + row.w - MS_TOGGLE_W - 40.0f, cy, MS_TOGGLE_W, 22.0f };
}

static ms_rect button_rect(const zcsr_game_settings* s, int idx) {
    ms_rect n;
    if (idx >= 0 && idx < 3 && ms_node_rect(s, s->btn_node[idx], &n)) return n;
    /* 0=Apply (right), 1=Cancel (right of Apply), 2=Reset (left) */
    float by = (float)s->bottom_y + (MS_BOTTOM_H - MS_BTN_H) * 0.5f;
    if (idx == 0) {
        return (ms_rect){ (float)(s->x + s->width - MS_PAD - MS_BTN_W), by, MS_BTN_W, MS_BTN_H };
    } else if (idx == 1) {
        return (ms_rect){ (float)(s->x + s->width - MS_PAD - MS_BTN_W * 2 - 12), by, MS_BTN_W, MS_BTN_H };
    } else {
        return (ms_rect){ (float)(s->x + MS_PAD), by, MS_BTN_W, MS_BTN_H };
    }
}

/* 把布局结果写回组件树：标签页、当前标签页的每一项、三个底部按钮。
 * 这里用纯公式计算，不读 node，避免"写的时候又依赖上一次写的结果"。 */
static void ms_sync_nodes(zcsr_game_settings* s) {
    if (!s->ui) return;

    float scale = s->text_scale;
    float cx = (float)s->x + MS_PAD;
    for (int t = 0; t < s->tab_count && t < MS_MAX_TABS; t++) {
        float tw = text_w(s->tabs[t].name, scale) + 32.0f;
        ZCSR_Rect r = { cx, (float)s->y + 4.0f, tw, (float)(MS_TAB_BAR_H - 8) };
        if (!s->tab_override[t]) zcsr_node_set_rect(s->ui, s->tab_node[t], &r);
        cx += tw + 4.0f;
    }

    int tab = s->active_tab;
    if (tab < 0) tab = 0;
    if (tab < s->tab_count && tab < MS_MAX_TABS) {
        int base = s->item_node_base[tab];
        for (int i = 0; i < s->tabs[tab].count && base + i < s->item_node_count; i++) {
            ZCSR_Rect r = {
                (float)s->content_left,
                (float)s->content_top + (float)i * (float)s->item_height - (float)s->scroll_offset,
                (float)s->content_w,
                (float)s->item_height
            };
            if (!s->item_override[base + i])
                zcsr_node_set_rect(s->ui, s->item_node[base + i], &r);
        }
    }

    float by = (float)s->bottom_y + (MS_BOTTOM_H - MS_BTN_H) * 0.5f;
    ZCSR_Rect b0 = { (float)(s->x + s->width - MS_PAD - MS_BTN_W), by, MS_BTN_W, MS_BTN_H };
    ZCSR_Rect b1 = { (float)(s->x + s->width - MS_PAD - MS_BTN_W * 2 - 12), by, MS_BTN_W, MS_BTN_H };
    ZCSR_Rect b2 = { (float)(s->x + MS_PAD), by, MS_BTN_W, MS_BTN_H };
    if (!s->btn_override[0]) zcsr_node_set_rect(s->ui, s->btn_node[0], &b0);
    if (!s->btn_override[1]) zcsr_node_set_rect(s->ui, s->btn_node[1], &b1);
    if (!s->btn_override[2]) zcsr_node_set_rect(s->ui, s->btn_node[2], &b2);
}

/* ---- recompute layout from current x/y/width/height (call after resize) ---- */
static void ms_recompute_layout(zcsr_game_settings* s) {
    if (!s) return;
    s->content_left = s->x + MS_PAD;
    s->content_top  = s->y + MS_TAB_BAR_H + MS_PAD;
    s->content_w    = s->width - MS_RIGHT_PANEL_W - MS_PAD * 3;
    s->content_h    = s->height - MS_TAB_BAR_H - MS_BOTTOM_H - MS_PAD * 2;
    if (s->content_w < 100) s->content_w = 100;
    if (s->content_h < 64)  s->content_h = 64;
    s->right_x      = s->x + s->width - MS_RIGHT_PANEL_W - MS_PAD;
    s->right_y      = s->content_top;
    s->right_w      = MS_RIGHT_PANEL_W;
    s->right_h      = s->content_h;
    s->bottom_y     = s->y + s->height - MS_BOTTOM_H;
    clamp_scroll(s);
    ms_sync_nodes(s);
}

/* ---- 创建 ---- */
zcsr_game_settings* zcsr_game_settings_create(void* buf, size_t bytes, const zcsr_game_settings_config* cfg) {
    if (!buf || !cfg || bytes < sizeof(zcsr_game_settings)) return NULL;
    if (!cfg->tabs || cfg->tab_count <= 0) return NULL;

    /* validate every tab and item */
    for (int t = 0; t < cfg->tab_count; t++) {
        const zcsr_game_settings_tab* tab = &cfg->tabs[t];
        if (!tab->items || tab->count < 0) return NULL;
        for (int i = 0; i < tab->count; i++) {
            const zcsr_game_settings_item* it = &tab->items[i];
            if (it->type < ZCSR_GAME_SETTINGS_DROPDOWN || it->type > ZCSR_GAME_SETTINGS_SEPARATOR) return NULL;
            if (it->type == ZCSR_GAME_SETTINGS_DROPDOWN) {
                if (it->option_count <= 0 || !it->options) return NULL;
                for (int o = 0; o < it->option_count; o++)
                    if (!it->options[o]) return NULL;
            }
            if (it->type == ZCSR_GAME_SETTINGS_SLIDER) {
                if (it->min >= it->max) return NULL;
                if (it->step <= 0.0f) return NULL;
            }
        }
    }

    zcsr_game_settings* s = (zcsr_game_settings*)buf;
    memset(s, 0, sizeof(*s));
    s->tabs        = cfg->tabs;
    s->tab_count   = cfg->tab_count;
    s->x           = cfg->x;
    s->y           = cfg->y;
    s->width       = (cfg->width > 0) ? cfg->width : 900;
    s->height      = (cfg->height > 0) ? cfg->height : 600;
    s->item_height = (cfg->item_height > 0) ? cfg->item_height : 32;
    s->text_scale  = (cfg->text_scale > 0.0f) ? cfg->text_scale : 1.4f;

    /* computed layout (with boundary protection) */
    ms_recompute_layout(s);

    /* init pending + default values for all tabs */
    for (int t = 0; t < s->tab_count; t++) {
        for (int i = 0; i < s->tabs[t].count; i++) {
            zcsr_game_settings_item* it = &s->tabs[t].items[i];
            it->pending_value  = it->value;
            it->pending_slider = it->slider_value;
            it->pending_toggle = it->toggle_value;
            it->default_value  = it->value;
            it->default_slider = it->slider_value;
            it->default_toggle = it->toggle_value;
            /* clamp slider default to [min,max] */
            if (it->type == ZCSR_GAME_SETTINGS_SLIDER) {
                if (it->default_slider < it->min) it->default_slider = it->min;
                if (it->default_slider > it->max) it->default_slider = it->max;
            }
            /* clamp dropdown default to [0, option_count-1] */
            if (it->type == ZCSR_GAME_SETTINGS_DROPDOWN && it->option_count > 0) {
                if (it->default_value < 0) it->default_value = 0;
                if (it->default_value >= it->option_count) it->default_value = it->option_count - 1;
                it->value = it->default_value;
                it->pending_value = it->default_value;
            }
        }
    }

    s->active_tab      = 0;
    s->selected_item   = first_selectable(s);
    s->hovered_tab     = -1;
    s->hovered_item    = -1;
    s->hovered_button  = -1;
    s->focus_area      = 0;
    s->selected_button = 0;
    s->dropdown_open   = -1;
    s->slider_drag     = -1;
    s->prev_buttons    = 0;
    s->scroll_offset   = 0;
    s->dirty           = false;

    /* ---- 建立组件树：每个标签页 / 每个设置项 / 每个底部按钮一个 node ----
     * 布局几何不再只存在于绘制代码里，而是每个控件自己持有一份 rect。 */
    {
        ZCSR_UIConfig ucfg;
        memset(&ucfg, 0, sizeof(ucfg));
        ucfg.max_nodes            = (uint32_t)(MS_MAX_ITEMS + MS_MAX_TABS + 8);
        ucfg.event_queue_capacity = 32;
        ucfg.draw_command_capacity = 64;
        ucfg.viewport_width       = (float)s->width;
        ucfg.viewport_height      = (float)s->height;

        if (zcsr_ui_create(&ucfg, &s->ui) == ZCSR_OK && s->ui) {
            (void)zcsr_node_create(s->ui, ZCSR_COMPONENT_PANEL, ZCSR_NODE_INVALID, &s->root_node);
            int used = 0;
            for (int t = 0; t < s->tab_count; t++) {
                if (t < MS_MAX_TABS) {
                    s->item_node_base[t] = used;
                    (void)zcsr_node_create(s->ui, ZCSR_COMPONENT_BUTTON, s->root_node, &s->tab_node[t]);
                }
                for (int i = 0; i < s->tabs[t].count; i++) {
                    if (used < MS_MAX_ITEMS) {
                        (void)zcsr_node_create(s->ui, ZCSR_COMPONENT_PANEL, s->root_node, &s->item_node[used]);
                        used++;
                    }
                }
            }
            s->item_node_count = used;
            for (int b = 0; b < 3; b++)
                (void)zcsr_node_create(s->ui, ZCSR_COMPONENT_BUTTON, s->root_node, &s->btn_node[b]);
            ms_sync_nodes(s);
        }
    }
    return s;
}

/* ---- runtime resize: update panel bounds and recompute layout ---- */
void zcsr_game_settings_set_bounds(zcsr_game_settings* s, int x, int y, int w, int h) {
    if (!s) return;
    if (w < 320) w = 320;
    if (h < 200) h = 200;
    s->x = x; s->y = y; s->width = w; s->height = h;
    ms_recompute_layout(s);
}

void zcsr_game_settings_destroy(zcsr_game_settings* s) {
    if (!s) return;
    if (s->ui) {
        zcsr_ui_destroy(s->ui);
        s->ui = NULL;
    }
}

/* ---- 滑块: 从鼠标 x 坐标设置值 ---- */
static void set_slider_from_mouse(zcsr_game_settings* s, int item_idx, int mx) {
    zcsr_game_settings_item* it = &cur_tab(s)->items[item_idx];
    ms_rect tr = slider_track_rect(s, item_idx);
    float ratio = ((float)mx - tr.x) / tr.w;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    float val = it->min + ratio * (it->max - it->min);
    if (it->step > 0.0f) {
        val = it->min + roundf((val - it->min) / it->step) * it->step;
    }
    if (val < it->min) val = it->min;
    if (val > it->max) val = it->max;
    it->pending_slider = val;
    recompute_dirty(s);
}

/* ---- 鼠标更新 (返回 1=apply, -1=cancel, 0=normal) ---- */
int zcsr_game_settings_update_mouse(zcsr_game_settings* s, const zcsr_mouse_state* mouse) {
    if (!s || !mouse) return 0;

    bool left_down = (mouse->buttons & ZCSR_MOUSE_LEFT) != 0;
    bool edge_down = left_down && !(s->prev_buttons & ZCSR_MOUSE_LEFT);
    bool edge_up   = !left_down && (s->prev_buttons & ZCSR_MOUSE_LEFT);
    int mx = mouse->x, my = mouse->y;
    int action = 0;

    /* --- slider drag (active regardless of position) --- */
    if (s->slider_drag >= 0) {
        if (left_down) {
            set_slider_from_mouse(s, s->slider_drag, mx);
        }
        if (edge_up) {
            s->slider_drag = -1;
        }
        s->prev_buttons = mouse->buttons;
        return 0;
    }

    /* --- dropdown open: capture all clicks --- */
    if (s->dropdown_open >= 0) {
        zcsr_game_settings_item* it = &cur_tab(s)->items[s->dropdown_open];
        if (edge_down) {
            /* click on closed box -> toggle close */
            ms_rect cb = dropdown_closed_rect(s, s->dropdown_open);
            if (point_in_rect(mx, my, cb)) {
                s->dropdown_open = -1;
            } else {
                /* check option clicks */
                bool hit = false;
                for (int o = 0; o < it->option_count; o++) {
                    if (point_in_rect(mx, my, dropdown_option_rect(s, s->dropdown_open, o))) {
                        it->pending_value = o;
                        s->dropdown_open = -1;
                        recompute_dirty(s);
                        hit = true;
                        break;
                    }
                }
                if (!hit) s->dropdown_open = -1; /* click outside closes */
            }
        } else {
            /* hover option */
            s->dropdown_hover_opt = -1;
            for (int o = 0; o < it->option_count; o++) {
                if (point_in_rect(mx, my, dropdown_option_rect(s, s->dropdown_open, o))) {
                    s->dropdown_hover_opt = o;
                    break;
                }
            }
        }
        s->prev_buttons = mouse->buttons;
        return 0;
    }

    /* --- mouse wheel scrolling --- */
    if (mouse->wheel != 0) {
        s->scroll_offset -= mouse->wheel * s->item_height;
        clamp_scroll(s);
    }

    /* --- normal: hover detection --- */
    s->hovered_tab = -1;
    s->hovered_item = -1;
    s->hovered_button = -1;

    for (int t = 0; t < s->tab_count; t++) {
        if (point_in_rect(mx, my, tab_rect(s, t))) { s->hovered_tab = t; break; }
    }

    zcsr_game_settings_tab* tab = cur_tab(s);
    for (int i = 0; i < tab->count; i++) {
        if (tab->items[i].type == ZCSR_GAME_SETTINGS_SEPARATOR) continue;
        ms_rect r = item_row_rect(s, i);
        if (r.y + r.h < s->content_top || r.y > s->content_top + s->content_h) continue;
        if (point_in_rect(mx, my, r)) { s->hovered_item = i; break; }
    }

    for (int b = 0; b < 3; b++) {
        if (point_in_rect(mx, my, button_rect(s, b))) { s->hovered_button = b; break; }
    }

    /* --- click handling --- */
    if (edge_down) {
        /* tab click */
        if (s->hovered_tab >= 0) {
            s->active_tab = s->hovered_tab;
            s->scroll_offset = 0;
            s->selected_item = first_selectable(s);
            s->dropdown_open = -1;
            s->focus_area = 0;
        }
        /* button click */
        else if (s->hovered_button >= 0) {
            if (s->hovered_button == 0) { action = 1; }  /* Apply: caller applies */
            else if (s->hovered_button == 1) { action = -1; }  /* Cancel: caller cancels */
            else if (s->hovered_button == 2) { zcsr_game_settings_reset(s); }  /* Reset: internal */
        }
        /* item click */
        else if (s->hovered_item >= 0) {
            s->selected_item = s->hovered_item;
            s->focus_area = 0;
            zcsr_game_settings_item* it = &tab->items[s->hovered_item];
            if (it->type == ZCSR_GAME_SETTINGS_DROPDOWN) {
                if (point_in_rect(mx, my, dropdown_closed_rect(s, s->hovered_item))) {
                    s->dropdown_open = s->hovered_item;
                    s->dropdown_up = compute_dropdown_up(s, s->hovered_item);
                    s->dropdown_hover_opt = it->pending_value;
                }
            } else if (it->type == ZCSR_GAME_SETTINGS_TOGGLE) {
                if (point_in_rect(mx, my, toggle_rect(s, s->hovered_item))) {
                    it->pending_toggle = !it->pending_toggle;
                    recompute_dirty(s);
                }
            } else if (it->type == ZCSR_GAME_SETTINGS_SLIDER) {
                ms_rect tr = slider_track_rect(s, s->hovered_item);
                /* expand hit area vertically for easier grabbing */
                ms_rect hit = { tr.x, tr.y - 8, tr.w, tr.h + 16 };
                if (point_in_rect(mx, my, hit)) {
                    s->slider_drag = s->hovered_item;
                    set_slider_from_mouse(s, s->hovered_item, mx);
                }
            }
        }
    }

    s->prev_buttons = mouse->buttons;
    return action;
}

/* ---- 键盘处理 (返回 1=apply, -1=cancel, 0=normal) ---- */
int zcsr_game_settings_handle_key(zcsr_game_settings* s, zcsr_keycode key, zcsr_key_action action) {
    if (!s || action != ZCSR_KEY_DOWN) return 0;

    /* --- dropdown open: capture keys --- */
    if (s->dropdown_open >= 0) {
        zcsr_game_settings_item* it = &cur_tab(s)->items[s->dropdown_open];
        switch (key) {
            case 38: case 87: /* Up / W */
                if (s->dropdown_hover_opt > 0) s->dropdown_hover_opt--;
                return 0;
            case 40: case 83: /* Down / S */
                if (s->dropdown_hover_opt < it->option_count - 1) s->dropdown_hover_opt++;
                return 0;
            case 13: case 32: /* Enter / Space */
                it->pending_value = s->dropdown_hover_opt;
                s->dropdown_open = -1;
                recompute_dirty(s);
                return 0;
            case 27: /* ESC */
                s->dropdown_open = -1;
                return 0;
        }
        return 0;
    }

    /* --- tab switching (Q/E) works in all modes except dropdown open --- */
    if (key == 81 || key == 69) { /* Q = prev, E = next */
        int dir = (key == 69) ? 1 : -1;
        s->active_tab = (s->active_tab + dir + s->tab_count) % s->tab_count;
        s->scroll_offset = 0;
        s->selected_item = first_selectable(s);
        s->hovered_item = -1;
        s->dropdown_open = -1;
        s->slider_drag = -1;
        return 0;
    }

    /* --- button focus mode --- */
    if (s->focus_area == 1) {
        switch (key) {
            case 37: case 65: /* Left / A */
                s->selected_button = (s->selected_button + 2) % 3; return 0;
            case 39: case 68: /* Right / D */
                s->selected_button = (s->selected_button + 1) % 3; return 0;
            case 13: case 32: /* Enter / Space — activate button */
                if (s->selected_button == 0) { return 1; }  /* Apply: caller applies */
                if (s->selected_button == 1) { return -1; } /* Cancel: caller cancels */
                if (s->selected_button == 2) { zcsr_game_settings_reset(s); return 0; } /* Reset: internal */
                return 0;
            case 38: case 87: /* Up / W -> back to items */
                s->focus_area = 0; return 0;
            case 9: /* Tab -> back to items */
                s->focus_area = 0; return 0;
            case 27: /* ESC -> cancel */
                return -1;
        }
        return 0;
    }

    /* --- item focus mode --- */
    switch (key) {
        case 40: case 83: { /* Down / S */
            int next = find_next_item(s, s->selected_item, 1);
            if (next >= 0) { s->selected_item = next; ensure_item_visible(s, next); }
            return 0;
        }
        case 38: case 87: { /* Up / W */
            int prev = find_next_item(s, s->selected_item, -1);
            if (prev >= 0) { s->selected_item = prev; ensure_item_visible(s, prev); }
            return 0;
        }
        case 39: case 68: { /* Right / D — increase */
            if (s->selected_item < 0) return 0;
            zcsr_game_settings_item* it = &cur_tab(s)->items[s->selected_item];
            if (it->type == ZCSR_GAME_SETTINGS_DROPDOWN) {
                if (it->pending_value < it->option_count - 1) it->pending_value++;
            } else if (it->type == ZCSR_GAME_SETTINGS_SLIDER) {
                it->pending_slider += it->step;
                if (it->pending_slider > it->max) it->pending_slider = it->max;
            } else if (it->type == ZCSR_GAME_SETTINGS_TOGGLE) {
                it->pending_toggle = true;
            }
            recompute_dirty(s);
            return 0;
        }
        case 37: case 65: { /* Left / A — decrease */
            if (s->selected_item < 0) return 0;
            zcsr_game_settings_item* it = &cur_tab(s)->items[s->selected_item];
            if (it->type == ZCSR_GAME_SETTINGS_DROPDOWN) {
                if (it->pending_value > 0) it->pending_value--;
            } else if (it->type == ZCSR_GAME_SETTINGS_SLIDER) {
                it->pending_slider -= it->step;
                if (it->pending_slider < it->min) it->pending_slider = it->min;
            } else if (it->type == ZCSR_GAME_SETTINGS_TOGGLE) {
                it->pending_toggle = false;
            }
            recompute_dirty(s);
            return 0;
        }
        case 13: case 32: { /* Enter / Space */
            if (s->selected_item < 0) return 0;
            zcsr_game_settings_item* it = &cur_tab(s)->items[s->selected_item];
            if (it->type == ZCSR_GAME_SETTINGS_DROPDOWN) {
                s->dropdown_open = s->selected_item;
                s->dropdown_up = compute_dropdown_up(s, s->selected_item);
                s->dropdown_hover_opt = it->pending_value;
            } else if (it->type == ZCSR_GAME_SETTINGS_TOGGLE) {
                it->pending_toggle = !it->pending_toggle;
                recompute_dirty(s);
            }
            return 0;
        }
        case 9: /* Tab -> buttons */
            s->focus_area = 1;
            s->selected_button = 0;
            return 0;
        case 27: /* ESC -> cancel */
            return -1;
    }
    return 0;
}

/* ---- 简单换行 (按空格拆分, 最多 max_lines) ---- */
static int wrap_text(const char* text, float max_w, float scale,
                     char lines[][128], int max_lines) {
    if (!text || !text[0]) return 0;
    int n = 0;
    const char* p = text;
    char line[128];
    int line_len = 0;
    line[0] = '\0';

    while (*p && n < max_lines) {
        /* copy word */
        char word[64];
        int wl = 0;
        while (*p && *p != ' ' && wl < 63) { word[wl++] = *p++; }
        word[wl] = '\0';
        if (*p == ' ') p++;

        int new_len = line_len + (line_len > 0 ? 1 : 0) + wl;
        if (new_len > 0 && (float)new_len * 8.0f * scale > max_w && line_len > 0) {
            strncpy(lines[n], line, 127); lines[n][127] = '\0';
            n++;
            line[0] = '\0'; line_len = 0;
        }
        if (line_len > 0) { line[line_len++] = ' '; }
        memcpy(line + line_len, word, wl + 1);
        line_len += wl;
    }
    if (line_len > 0 && n < max_lines) {
        strncpy(lines[n], line, 127); lines[n][127] = '\0';
        n++;
    }
    return n;
}

/* ---- 绘制下拉箭头 (V 形) ---- */
static void draw_dd_arrow(zcsr_gl_renderer* r, float cx, float cy, float r0, float g0, float b0, float a0) {
    zcsr_gl_draw_line(r, cx - 4, cy - 2, cx, cy + 3, 1.5f, r0, g0, b0, a0);
    zcsr_gl_draw_line(r, cx, cy + 3, cx + 4, cy - 2, 1.5f, r0, g0, b0, a0);
}

/* ---- 渲染 ---- */
void zcsr_game_settings_render(zcsr_game_settings* s, zcsr_gl_renderer* r) {
    if (!s || !r) return;
    float scale = s->text_scale;

    /* === panel background === */
    zcsr_gl_fill_rect(r, (float)s->x, (float)s->y, (float)s->width, (float)s->height, C_BG_PANEL);
    /* border */
    zcsr_gl_draw_rect(r, (float)s->x, (float)s->y, (float)s->width, (float)s->height, 1.0f, C_BORDER);

    /* === tab bar === */
    zcsr_gl_fill_rect(r, (float)s->x, (float)s->y, (float)s->width, (float)MS_TAB_BAR_H, C_BG_TABBAR);
    /* tab bar bottom line */
    zcsr_gl_fill_rect(r, (float)s->x, (float)s->y + MS_TAB_BAR_H - 1, (float)s->width, 1.0f, C_BORDER);

    for (int t = 0; t < s->tab_count; t++) {
        ms_rect tr = tab_rect(s, t);
        bool active = (t == s->active_tab);
        bool hover = (t == s->hovered_tab);
        if (active) {
            zcsr_gl_fill_rect(r, tr.x, tr.y, tr.w, tr.h, C_BG_TAB_ACT);
            /* accent underline */
            zcsr_gl_fill_rect(r, tr.x, tr.y + tr.h - 2, tr.w, 2.0f, C_ACCENT);
        } else if (hover) {
            zcsr_gl_fill_rect(r, tr.x, tr.y, tr.w, tr.h, C_BG_TAB_HOV);
        }
        float tw = text_w(s->tabs[t].name, scale);
        zcsr_gl_draw_text(r, s->tabs[t].name,
                          tr.x + (tr.w - tw) * 0.5f, tr.y + (tr.h - 8.0f * scale) * 0.5f,
                          scale,
                          MS_RGBA_IF(active, 1.0f,1.0f,1.0f,1.0f, (hover?0.72f:0.42f),(hover?0.76f:0.46f),(hover?0.82f:0.52f),1.0f));
    }

    /* === content area background === */
    zcsr_gl_fill_rect(r, (float)s->content_left - 4, (float)s->content_top - 4,
                      (float)s->content_w + 8, (float)s->content_h + 8, C_BG_CONTENT);

    /* === right panel === */
    zcsr_gl_fill_rect(r, (float)s->right_x - 4, (float)s->right_y - 4,
                      (float)s->right_w + 8, (float)s->right_h + 8, 0.03f, 0.05f, 0.08f, 1.0f);
    zcsr_gl_draw_rect(r, (float)s->right_x - 4, (float)s->right_y - 4,
                      (float)s->right_w + 8, (float)s->right_h + 8, 1.0f, C_BORDER);

    /* determine info item: hovered takes priority, else selected */
    int info_idx = (s->hovered_item >= 0) ? s->hovered_item : s->selected_item;
    zcsr_game_settings_tab* tab = cur_tab(s);
    if (info_idx >= 0 && info_idx < tab->count) {
        zcsr_game_settings_item* it = &tab->items[info_idx];
        float iy = (float)s->right_y + 12.0f;
        /* title */
        zcsr_gl_draw_text(r, it->label, (float)s->right_x + 8, iy, scale * 1.1f, C_TXT_ACCENT);
        iy += 8.0f * scale * 1.1f + 8.0f;
        /* separator */
        zcsr_gl_fill_rect(r, (float)s->right_x + 8, iy, (float)s->right_w - 16, 1.0f, C_BORDER);
        iy += 10.0f;
        /* description (wrapped) */
        if (it->description && it->description[0]) {
            char lines[6][128];
            int nl = wrap_text(it->description, (float)s->right_w - 24, scale * 0.85f, lines, 6);
            for (int l = 0; l < nl; l++) {
                zcsr_gl_draw_text(r, lines[l], (float)s->right_x + 8, iy, scale * 0.85f, C_TXT_NORM);
                iy += 8.0f * scale * 0.85f + 4.0f;
            }
        } else {
            zcsr_gl_draw_text(r, "(no description)", (float)s->right_x + 8, iy, scale * 0.85f, C_TXT_DIM);
        }

        /* current value display */
        iy += 12.0f;
        zcsr_gl_fill_rect(r, (float)s->right_x + 8, iy, (float)s->right_w - 16, 1.0f, C_BORDER);
        iy += 10.0f;
        zcsr_gl_draw_text(r, "Current:", (float)s->right_x + 8, iy, scale * 0.85f, C_TXT_DIM);
        iy += 8.0f * scale * 0.85f + 4.0f;
        char valbuf[64];
        if (it->type == ZCSR_GAME_SETTINGS_DROPDOWN) {
            const char* v = (it->pending_value >= 0 && it->pending_value < it->option_count)
                            ? it->options[it->pending_value] : "?";
            snprintf(valbuf, sizeof(valbuf), "%s", v);
        } else if (it->type == ZCSR_GAME_SETTINGS_SLIDER) {
            snprintf(valbuf, sizeof(valbuf), "%.1f", it->pending_slider);
        } else if (it->type == ZCSR_GAME_SETTINGS_TOGGLE) {
            snprintf(valbuf, sizeof(valbuf), "%s", it->pending_toggle ? "On" : "Off");
        } else {
            valbuf[0] = '\0';
        }
        zcsr_gl_draw_text(r, valbuf, (float)s->right_x + 8, iy, scale, C_TXT_BRIGHT);
    }

    /* === settings items === */
    for (int i = 0; i < tab->count; i++) {
        zcsr_game_settings_item* it = &tab->items[i];
        ms_rect row = item_row_rect(s, i);

        /* skip items outside content area */
        if (row.y + row.h < s->content_top || row.y > s->content_top + s->content_h) continue;

        if (it->type == ZCSR_GAME_SETTINGS_SEPARATOR) {
            /* group label */
            zcsr_gl_draw_text(r, it->label, row.x, row.y + 4, scale * 0.8f, C_TXT_DIM);
            /* line */
            float lw = text_w(it->label, scale * 0.8f);
            zcsr_gl_fill_rect(r, row.x + lw + 8, row.y + row.h * 0.5f,
                              row.w - lw - 8, 1.0f, C_BORDER);
            continue;
        }

        bool is_sel = (i == s->selected_item && s->focus_area == 0);
        bool is_hov = (i == s->hovered_item);

        /* row highlight */
        if (is_sel) {
            zcsr_gl_fill_rect(r, row.x - 4, row.y, row.w + 8, row.h, C_BG_ROW_SEL);
            zcsr_gl_fill_rect(r, row.x - 4, row.y, 3.0f, row.h, C_ACCENT);
        } else if (is_hov) {
            zcsr_gl_fill_rect(r, row.x - 4, row.y, row.w + 8, row.h, C_BG_ROW_HOV);
        }

        /* label */
        {
            zcsr_game_settings_color lc;
            zcsr_game_settings_get_item_color(s, s->active_tab, i, &lc);
            zcsr_gl_draw_text(r, it->label, row.x + 4, row.y + (row.h - 8.0f * scale) * 0.5f,
                              scale, MS_RGBA_IF(is_sel, 1.0f,1.0f,1.0f,1.0f, lc.r,lc.g,lc.b,1.0f));
        }

        /* dirty marker */
        bool item_dirty = false;
        if (it->type == ZCSR_GAME_SETTINGS_DROPDOWN) item_dirty = (it->pending_value != it->value);
        else if (it->type == ZCSR_GAME_SETTINGS_SLIDER) item_dirty = (fabsf(it->pending_slider - it->slider_value) > 0.001f);
        else if (it->type == ZCSR_GAME_SETTINGS_TOGGLE) item_dirty = (it->pending_toggle != it->toggle_value);
        if (item_dirty) {
            zcsr_gl_fill_rect(r, row.x + row.w + 4, row.y + row.h * 0.5f - 2, 4.0f, 4.0f, C_DIRTY);
        }

        /* --- control rendering --- */
        if (it->type == ZCSR_GAME_SETTINGS_DROPDOWN) {
            ms_rect cb = dropdown_closed_rect(s, i);
            bool dd_open = (s->dropdown_open == i);
            bool hov = is_hov || dd_open;
            zcsr_gl_fill_rect(r, cb.x, cb.y, cb.w, cb.h, MS_RGBA_IF(hov, 0.12f,0.16f,0.24f,1.0f, 0.08f,0.11f,0.16f,1.0f));
            zcsr_gl_draw_rect(r, cb.x, cb.y, cb.w, cb.h, 1.0f, MS_RGBA_IF(hov, 0.38f,0.52f,0.78f,1.0f, 0.22f,0.28f,0.38f,1.0f));
            const char* val = (it->pending_value >= 0 && it->pending_value < it->option_count)
                              ? it->options[it->pending_value] : "?";
            zcsr_gl_draw_text(r, val, cb.x + 8, cb.y + (cb.h - 8.0f * scale) * 0.5f,
                              scale * 0.9f, C_TXT_BRIGHT);
            draw_dd_arrow(r, cb.x + cb.w - 12, cb.y + cb.h * 0.5f,
                          hov ? 0.5f : 0.35f, hov ? 0.7f : 0.5f, hov ? 1.0f : 0.7f, 1.0f);
        } else if (it->type == ZCSR_GAME_SETTINGS_SLIDER) {
            ms_rect tr = slider_track_rect(s, i);
            /* value text */
            char vbuf[16];
            snprintf(vbuf, sizeof(vbuf), "%.0f", it->pending_slider);
            float vw = text_w(vbuf, scale * 0.9f);
            zcsr_gl_draw_text(r, vbuf, tr.x - vw - 8, tr.y - 5, scale * 0.9f,
                              MS_RGBA_IF(is_sel, 1.0f,1.0f,1.0f,1.0f, 0.72f,0.76f,0.82f,1.0f));
            /* track */
            zcsr_gl_fill_rect(r, tr.x, tr.y, tr.w, tr.h, 0.12f, 0.14f, 0.18f, 1.0f);
            /* fill */
            float ratio = (it->max > it->min) ? (it->pending_slider - it->min) / (it->max - it->min) : 0.0f;
            if (ratio < 0) ratio = 0; if (ratio > 1) ratio = 1;
            zcsr_gl_fill_rect(r, tr.x, tr.y, tr.w * ratio, tr.h,
                              MS_RGBA_IF(is_sel, 0.35f,0.65f,1.0f,1.0f, 0.25f,0.55f,0.95f,1.0f));
            /* handle */
            float hx = tr.x + tr.w * ratio - 4;
            zcsr_gl_fill_rect(r, hx, tr.y - 4, 8.0f, tr.h + 8,
                              MS_RGBA_IF(is_sel, 1.0f,1.0f,1.0f,1.0f, 0.85f,0.9f,1.0f,1.0f));
        } else if (it->type == ZCSR_GAME_SETTINGS_TOGGLE) {
            ms_rect tg = toggle_rect(s, i);
            bool on = it->pending_toggle;
            /* track */
            zcsr_gl_fill_rect(r, tg.x, tg.y, tg.w, tg.h, MS_RGBA_IF(on, 0.20f,0.60f,0.35f,1.0f, 0.25f,0.25f,0.30f,1.0f));
            zcsr_gl_draw_rect(r, tg.x, tg.y, tg.w, tg.h, 1.0f, MS_RGBA_IF(on, 0.3f,0.8f,0.5f,1.0f, 0.22f,0.28f,0.38f,1.0f));
            /* knob */
            float kx = on ? tg.x + tg.w - tg.h + 2 : tg.x + 2;
            zcsr_gl_fill_rect(r, kx, tg.y + 2, tg.h - 4, tg.h - 4, C_TXT_BRIGHT);
            /* state text */
            zcsr_gl_draw_text(r, on ? "On" : "Off", tg.x + tg.w + 8, tg.y + (tg.h - 8.0f * scale) * 0.5f,
                              scale * 0.9f, MS_RGBA_IF(on, 0.4f,0.9f,0.6f,1.0f, 0.42f,0.46f,0.52f,1.0f));
        }
    }

    /* === dropdown popup (render on top) === */
    if (s->dropdown_open >= 0) {
        zcsr_game_settings_item* it = &cur_tab(s)->items[s->dropdown_open];
        ms_rect cb = dropdown_closed_rect(s, s->dropdown_open);
        float pop_h = (float)it->option_count * MS_DD_OPT_H;
        float pop_y = s->dropdown_up ? cb.y - pop_h : cb.y + cb.h;
        /* popup background */
        zcsr_gl_fill_rect(r, cb.x, pop_y, cb.w, pop_h, 0.10f, 0.13f, 0.19f, 0.98f);
        zcsr_gl_draw_rect(r, cb.x, pop_y, cb.w, pop_h, 1.0f, C_BORDER_HOV);
        for (int o = 0; o < it->option_count; o++) {
            ms_rect or = dropdown_option_rect(s, s->dropdown_open, o);
            bool hov = (o == s->dropdown_hover_opt);
            bool cur = (o == it->pending_value);
            if (hov) {
                zcsr_gl_fill_rect(r, or.x, or.y, or.w, or.h, C_ACCENT_DIM);
            }
            if (cur) {
                zcsr_gl_fill_rect(r, or.x, or.y, 3.0f, or.h, C_ACCENT);
            }
            zcsr_gl_draw_text(r, it->options[o], or.x + 10, or.y + (or.h - 8.0f * scale * 0.9f) * 0.5f,
                              scale * 0.9f,
                              MS_RGBA_IF(hov, 1.0f,1.0f,1.0f,1.0f, (cur?0.40f:0.72f),(cur?0.68f:0.76f),(cur?1.0f:0.82f),1.0f));
        }
    }

    /* === bottom bar === */
    zcsr_gl_fill_rect(r, (float)s->x, (float)s->bottom_y, (float)s->width, (float)MS_BOTTOM_H,
                      0.03f, 0.04f, 0.07f, 1.0f);
    zcsr_gl_fill_rect(r, (float)s->x, (float)s->bottom_y, (float)s->width, 1.0f, C_BORDER);

    const char* btn_labels[3] = { "Apply", "Cancel", "Reset" };
    for (int b = 0; b < 3; b++) {
        ms_rect br = button_rect(s, b);
        bool hov = (b == s->hovered_button);
        bool foc = (b == s->selected_button && s->focus_area == 1);
        bool primary = (b == 0); /* Apply is primary */
        {
            zcsr_game_settings_color bc;
            zcsr_game_settings_get_button_color(s, b, &bc);
            /* hover 时提亮 30%，颜色本身仍然来自组件（可能被编辑器改过） */
            float kr = hov ? 1.3f : 1.0f;
            zcsr_gl_fill_rect(r, br.x, br.y, br.w, br.h,
                              bc.r * kr > 1.0f ? 1.0f : bc.r * kr,
                              bc.g * kr > 1.0f ? 1.0f : bc.g * kr,
                              bc.b * kr > 1.0f ? 1.0f : bc.b * kr, 1.0f);
        }
        if (foc) {
            zcsr_gl_draw_rect(r, br.x - 1, br.y - 1, br.w + 2, br.h + 2, 2.0f, C_ACCENT_GLOW);
        } else {
            zcsr_gl_draw_rect(r, br.x, br.y, br.w, br.h, 1.0f, MS_RGBA_IF(primary, 0.35f,0.65f,1.0f,1.0f, 0.22f,0.28f,0.38f,1.0f));
        }
        float bw = text_w(btn_labels[b], scale);
        zcsr_gl_draw_text(r, btn_labels[b], br.x + (br.w - bw) * 0.5f,
                          br.y + (br.h - 8.0f * scale) * 0.5f, scale,
                          MS_RGBA_IF(primary, 1.0f,1.0f,1.0f,1.0f, (hov?1.0f:0.72f),(hov?1.0f:0.76f),(hov?1.0f:0.82f),1.0f));
    }

    /* dirty indicator */
    if (s->dirty) {
        zcsr_gl_draw_text(r, "* unsaved changes",
                          (float)(s->x + MS_PAD + MS_BTN_W + 16),
                          (float)s->bottom_y + (MS_BOTTOM_H - 8.0f * scale * 0.85f) * 0.5f,
                          scale * 0.85f, C_DIRTY);
    }
}

/* ---- apply / cancel / reset ---- */
void zcsr_game_settings_apply(zcsr_game_settings* s) {
    if (!s) return;
    for (int t = 0; t < s->tab_count; t++) {
        for (int i = 0; i < s->tabs[t].count; i++) {
            s->tabs[t].items[i].value        = s->tabs[t].items[i].pending_value;
            s->tabs[t].items[i].slider_value = s->tabs[t].items[i].pending_slider;
            s->tabs[t].items[i].toggle_value = s->tabs[t].items[i].pending_toggle;
        }
    }
    s->dirty = false;
}

void zcsr_game_settings_cancel(zcsr_game_settings* s) {
    if (!s) return;
    for (int t = 0; t < s->tab_count; t++) {
        for (int i = 0; i < s->tabs[t].count; i++) {
            s->tabs[t].items[i].pending_value  = s->tabs[t].items[i].value;
            s->tabs[t].items[i].pending_slider = s->tabs[t].items[i].slider_value;
            s->tabs[t].items[i].pending_toggle = s->tabs[t].items[i].toggle_value;
        }
    }
    s->dropdown_open = -1;
    s->slider_drag = -1;
    s->dirty = false;
}

void zcsr_game_settings_reset(zcsr_game_settings* s) {
    if (!s) return;
    for (int t = 0; t < s->tab_count; t++) {
        for (int i = 0; i < s->tabs[t].count; i++) {
            zcsr_game_settings_item* it = &s->tabs[t].items[i];
            it->value         = it->default_value;
            it->slider_value  = it->default_slider;
            it->toggle_value  = it->default_toggle;
            it->pending_value  = it->default_value;
            it->pending_slider = it->default_slider;
            it->pending_toggle = it->default_toggle;
        }
    }
    s->dropdown_open = -1;
    s->slider_drag = -1;
    s->dirty = false;
}

bool zcsr_game_settings_is_dirty(const zcsr_game_settings* s) { return s ? s->dirty : false; }
int  zcsr_game_settings_active_tab(const zcsr_game_settings* s) { return s ? s->active_tab : -1; }

/* ---- 编辑器接入：外部几何覆盖 ---- */
static bool write_node_rect(zcsr_game_settings* s, ZCSR_NodeId id, const zcsr_game_settings_rect* r) {
    ZCSR_Rect rr;
    if (!s || !s->ui || !r || (uint32_t)id == 0u) return false;
    rr.x = r->x; rr.y = r->y; rr.width = r->w; rr.height = r->h;
    return zcsr_node_set_rect(s->ui, id, &rr) == ZCSR_OK;
}

bool zcsr_game_settings_override_tab(zcsr_game_settings* s, int tab_index,
                                     const zcsr_game_settings_rect* rect) {
    if (!s || tab_index < 0 || tab_index >= s->tab_count || tab_index >= MS_MAX_TABS) return false;
    if (!rect) {
        s->tab_override[tab_index] = 0;
        ms_sync_nodes(s);
        return true;
    }
    if (!write_node_rect(s, s->tab_node[tab_index], rect)) return false;
    s->tab_override[tab_index] = 1;
    return true;
}

bool zcsr_game_settings_override_item(zcsr_game_settings* s, int tab_index, int item_index,
                                      const zcsr_game_settings_rect* rect) {
    if (!s || tab_index < 0 || tab_index >= s->tab_count || tab_index >= MS_MAX_TABS) return false;
    if (item_index < 0 || item_index >= s->tabs[tab_index].count) return false;
    int flat = s->item_node_base[tab_index] + item_index;
    if (flat < 0 || flat >= s->item_node_count || flat >= MS_MAX_ITEMS) return false;
    if (!rect) {
        s->item_override[flat] = 0;
        ms_sync_nodes(s);
        return true;
    }
    if (!write_node_rect(s, s->item_node[flat], rect)) return false;
    s->item_override[flat] = 1;
    return true;
}

bool zcsr_game_settings_override_button(zcsr_game_settings* s, int button_index,
                                        const zcsr_game_settings_rect* rect) {
    if (!s || button_index < 0 || button_index >= 3) return false;
    if (!rect) {
        s->btn_override[button_index] = 0;
        ms_sync_nodes(s);
        return true;
    }
    if (!write_node_rect(s, s->btn_node[button_index], rect)) return false;
    s->btn_override[button_index] = 1;
    return true;
}

int zcsr_game_settings_tab_count(const zcsr_game_settings* s) { return s ? s->tab_count : 0; }

const char* zcsr_game_settings_tab_name(const zcsr_game_settings* s, int tab_index) {
    if (!s || tab_index < 0 || tab_index >= s->tab_count) return NULL;
    return s->tabs[tab_index].name;
}

int zcsr_game_settings_item_count(const zcsr_game_settings* s, int tab_index) {
    if (!s || tab_index < 0 || tab_index >= s->tab_count) return 0;
    return s->tabs[tab_index].count;
}

/* 读取某一项"已应用"的值（不是 pending）：调用方拥有 items 数组，但跨模块时
 * 只能通过这个接口拿值 —— 这就是此前 15 项"只存不生效"的根：没有消费方读值的口子。 */
bool zcsr_game_settings_get_item_value(const zcsr_game_settings* s, int tab_index, int item_index,
                                       int* value, float* slider, bool* toggle) {
    if (!s || tab_index < 0 || tab_index >= s->tab_count) return false;
    if (item_index < 0 || item_index >= s->tabs[tab_index].count) return false;
    const zcsr_game_settings_item* it = &s->tabs[tab_index].items[item_index];
    if (value)  *value  = it->value;
    if (slider) *slider = it->slider_value;
    if (toggle) *toggle = it->toggle_value;
    return true;
}
const char* zcsr_game_settings_item_label(const zcsr_game_settings* s, int tab_index, int item_index) {
    if (!s || tab_index < 0 || tab_index >= s->tab_count) return NULL;
    if (item_index < 0 || item_index >= s->tabs[tab_index].count) return NULL;
    return s->tabs[tab_index].items[item_index].label;
}

const char* zcsr_game_settings_button_name(int button_index) {
    switch (button_index) {
        case 0: return "Apply";
        case 1: return "Cancel";
        case 2: return "Reset";
        default: return NULL;
    }
}
bool zcsr_game_settings_get_tab_rect(const zcsr_game_settings* s, int tab_index,
                                     zcsr_game_settings_rect* out) {
    ms_rect r;
    if (!s || !out || tab_index < 0 || tab_index >= s->tab_count || tab_index >= MS_MAX_TABS) return false;
    r = tab_rect(s, tab_index);
    *out = (zcsr_game_settings_rect){ r.x, r.y, r.w, r.h };
    return true;
}

bool zcsr_game_settings_get_item_rect(const zcsr_game_settings* s, int tab_index, int item_index,
                                      zcsr_game_settings_rect* out) {
    ms_rect r;
    ZCSR_Rect nr;
    int flat;
    if (!s || !out || tab_index < 0 || tab_index >= s->tab_count) return false;
    if (item_index < 0 || item_index >= s->tabs[tab_index].count) return false;
    flat = s->item_node_base[tab_index] + item_index;

    if (flat < 0 || flat >= s->item_node_count || flat >= MS_MAX_ITEMS) return false;
    if (!s->ui || (uint32_t)s->item_node[flat] == 0u) return false;
    if (zcsr_node_get_rect(s->ui, s->item_node[flat], &nr) != ZCSR_OK) return false;
    r.x = nr.x; r.y = nr.y; r.w = nr.width; r.h = nr.height;
    *out = (zcsr_game_settings_rect){ r.x, r.y, r.w, r.h };
    return true;
}

bool zcsr_game_settings_get_button_rect(const zcsr_game_settings* s, int button_index,
                                        zcsr_game_settings_rect* out) {
    ms_rect r;
    if (!s || !out || button_index < 0 || button_index >= 3) return false;
    r = button_rect(s, button_index);
    *out = (zcsr_game_settings_rect){ r.x, r.y, r.w, r.h };
    return true;
}/* ---- 布局持久化 ------------------------------------------------------------
 * 编辑器退出编辑态时调用 save，把"人改过的几何"写成纯文本；下次启动 load 回来。
 * 只写被 override 的控件，公式布局保持随窗口自适应。 */
bool zcsr_game_settings_save_overrides(const zcsr_game_settings* s, const char* path)
{
    FILE* f;
    int t, i, flat, written = 0;

    if (!s || !path) return false;
    f = fopen(path, "wb");
    if (!f) return false;

    flat = 0;
    for (t = 0; t < s->tab_count; ++t) {
        zcsr_game_settings_rect r;
        if (s->tab_override[t] && zcsr_game_settings_get_tab_rect(s, t, &r)) {
            fprintf(f, "tab %d %.2f %.2f %.2f %.2f\n", t, r.x, r.y, r.w, r.h);
            ++written;
        }
        for (i = 0; i < s->tabs[t].count && flat < MS_MAX_ITEMS; ++i, ++flat) {
            if (s->item_override[flat] &&
                zcsr_game_settings_get_item_rect(s, t, i, &r)) {
                fprintf(f, "item %d %d %.2f %.2f %.2f %.2f\n", t, i, r.x, r.y, r.w, r.h);
                ++written;
            }
            if (s->item_color_set[flat]) {
                fprintf(f, "itemcolor %d %d %.3f %.3f %.3f\n", t, i,
                        s->item_color[flat][0], s->item_color[flat][1], s->item_color[flat][2]);
                ++written;
            }
        }
    }
    for (i = 0; i < 3; ++i) {
        zcsr_game_settings_rect r;
        if (s->btn_override[i] && zcsr_game_settings_get_button_rect(s, i, &r)) {
            fprintf(f, "btn %d %.2f %.2f %.2f %.2f\n", i, r.x, r.y, r.w, r.h);
            ++written;
        }
        if (s->btn_color_set[i]) {
            fprintf(f, "btncolor %d %.3f %.3f %.3f\n", i,
                    s->btn_color[i][0], s->btn_color[i][1], s->btn_color[i][2]);
            ++written;
        }
    }
    fclose(f);
    return written > 0;   /* 空文件 = 没东西可存，调用方据此提示"nothing to save" */
}

bool zcsr_game_settings_load_overrides(zcsr_game_settings* s, const char* path)
{
    FILE* f;
    char kind[8];
    int a = 0, b = 0, applied = 0;
    zcsr_game_settings_rect r;

    if (!s || !path) return false;
    f = fopen(path, "rb");
    if (!f) return false;
    while (fscanf(f, "%7s", kind) == 1) {
        if (strcmp(kind, "tab") == 0) {
            if (fscanf(f, "%d %f %f %f %f", &a, &r.x, &r.y, &r.w, &r.h) != 5) break;
            if (zcsr_game_settings_override_tab(s, a, &r)) ++applied;
        } else if (strcmp(kind, "item") == 0) {
            if (fscanf(f, "%d %d %f %f %f %f", &a, &b, &r.x, &r.y, &r.w, &r.h) != 6) break;
            if (zcsr_game_settings_override_item(s, a, b, &r)) ++applied;
        } else if (strcmp(kind, "btn") == 0) {
            if (fscanf(f, "%d %f %f %f %f", &a, &r.x, &r.y, &r.w, &r.h) != 5) break;
            if (zcsr_game_settings_override_button(s, a, &r)) ++applied;
        } else if (strcmp(kind, "itemcolor") == 0) {
            zcsr_game_settings_color c;
            if (fscanf(f, "%d %d %f %f %f", &a, &b, &c.r, &c.g, &c.b) != 5) break;
            if (zcsr_game_settings_override_item_color(s, a, b, &c)) ++applied;
        } else if (strcmp(kind, "btncolor") == 0) {
            zcsr_game_settings_color c;
            if (fscanf(f, "%d %f %f %f", &a, &c.r, &c.g, &c.b) != 4) break;
            if (zcsr_game_settings_override_button_color(s, a, &c)) ++applied;
        } else {
            break;
        }
    }
    fclose(f);
    return applied > 0;
}
/* ---- 组件颜色覆盖 --------------------------------------------------------- */
static int ms_flat_item_index(const zcsr_game_settings* s, int tab_index, int item_index)
{
    int t, flat = 0;
    if (!s || tab_index < 0 || tab_index >= s->tab_count) return -1;
    if (item_index < 0 || item_index >= s->tabs[tab_index].count) return -1;
    for (t = 0; t < tab_index; ++t) flat += s->tabs[t].count;
    return flat + item_index;
}

bool zcsr_game_settings_override_item_color(zcsr_game_settings* s, int tab_index, int item_index,
                                            const zcsr_game_settings_color* color)
{
    int flat = ms_flat_item_index(s, tab_index, item_index);
    if (flat < 0 || flat >= MS_MAX_ITEMS) return false;
    if (!color) { s->item_color_set[flat] = 0; return true; }
    s->item_color_set[flat] = 1;
    s->item_color[flat][0] = color->r;
    s->item_color[flat][1] = color->g;
    s->item_color[flat][2] = color->b;
    return true;
}

bool zcsr_game_settings_override_button_color(zcsr_game_settings* s, int button_index,
                                              const zcsr_game_settings_color* color)
{
    if (!s || button_index < 0 || button_index >= 3) return false;
    if (!color) { s->btn_color_set[button_index] = 0; return true; }
    s->btn_color_set[button_index] = 1;
    s->btn_color[button_index][0] = color->r;
    s->btn_color[button_index][1] = color->g;
    s->btn_color[button_index][2] = color->b;
    return true;
}

bool zcsr_game_settings_get_item_color(const zcsr_game_settings* s, int tab_index, int item_index,
                                       zcsr_game_settings_color* out)
{
    int flat = ms_flat_item_index(s, tab_index, item_index);
    if (flat < 0 || !out) return false;
    if (s->item_color_set[flat]) {
        out->r = s->item_color[flat][0];
        out->g = s->item_color[flat][1];
        out->b = s->item_color[flat][2];
    } else {
        out->r = 0.72f; out->g = 0.76f; out->b = 0.82f;   /* 主题正文色 */
    }
    return true;
}

bool zcsr_game_settings_get_button_color(const zcsr_game_settings* s, int button_index,
                                         zcsr_game_settings_color* out)
{
    if (!s || button_index < 0 || button_index >= 3 || !out) return false;
    if (s->btn_color_set[button_index]) {
        out->r = s->btn_color[button_index][0];
        out->g = s->btn_color[button_index][1];
        out->b = s->btn_color[button_index][2];
    } else if (button_index == 0) {
        out->r = 0.25f; out->g = 0.55f; out->b = 0.95f;   /* Apply 主色 */
    } else {
        out->r = 0.08f; out->g = 0.11f; out->b = 0.16f;
    }
    return true;
}
