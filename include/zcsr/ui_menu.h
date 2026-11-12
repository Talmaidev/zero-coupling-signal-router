/* integration/menu_ui.h — 通用菜单 UI 组件 (基于 glrender + input).
 *
 * 可复用的垂直菜单: 标题 + 多个菜单项, 支持键盘导航和高亮.
 *
 * 特性:
 *   - 键盘上下导航 (Up/Down / W/S)
 *   - Enter/Space 确认, ESC 取消
 *   - 当前选中项高亮 (背景色 + 文字色)
 *   - 支持禁用项 (跳过, 灰色显示)
 *   - 自定义位置/尺寸/颜色
 *   - 零堆分配: 调用者提供内存
 *
 * 用法:
 *   zcsr_menu_item items[] = {
 *     { "Start Game",  MENU_START,  false },
 *     { "Options",     MENU_OPTS,   false },
 *     { "Quit",        MENU_QUIT,   false },
 *   };
 *   zcsr_menu* menu = zcsr_menu_create(buf, sizeof buf, &(zcsr_menu_config){
 *     .title = "MAIN MENU",
 *     .items = items, .item_count = 3,
 *     .x = 100, .y = 100, .item_height = 40,
 *   });
 *   // 主循环:
 *   zcsr_menu_handle_key(menu, key, action);  // 返回选中 id 或 -1
 *   zcsr_menu_render(menu, renderer);
 */
#ifndef ZCSR_MENU_UI_H
#define ZCSR_MENU_UI_H

#include "zcsr/gl_render.h"
#include "zcsr/input.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 一个菜单项 */
typedef struct {
    const char* label;   /* 显示文字 */
    int         id;      /* 选中时返回的 ID (调用者自定义, 建议 >= 0) */
    bool        disabled;/* 禁用: 不可选中, 灰色显示 */
} zcsr_menu_item;

/* 菜单配置 (调用者拥有, create 时复制所需字段) */
typedef struct {
    const char*         title;       /* 标题 (NULL 表示无标题) */
    const zcsr_menu_item* items;     /* 菜单项数组 */
    int                 item_count;  /* 菜单项数量 */
    int                 x;           /* 菜单左上角 x */
    int                 y;           /* 菜单左上角 y (标题位置) */
    int                 item_height; /* 每项高度 (像素, 默认 36) */
    float               text_scale;  /* 文字缩放 (默认 2.0) */

    /* 颜色 (均可为默认, 传 0 则用默认值) */
    float title_r, title_g, title_b;       /* 标题颜色, 默认 1.0,0.9,0.5 */
    float item_r,  item_g,  item_b;        /* 普通项文字色, 默认 0.8,0.8,0.8 */
    float sel_r,   sel_g,   sel_b;         /* 选中项文字色, 默认 1.0,1.0,1.0 */
    float sel_bg_r, sel_bg_g, sel_bg_b;    /* 选中项背景色, 默认 0.2,0.4,0.8 */
    float disabled_r, disabled_g, disabled_b; /* 禁用项文字色, 默认 0.4,0.4,0.4 */
} zcsr_menu_config;

typedef struct zcsr_menu zcsr_menu;

/* 创建菜单. buf 由调用者提供, 至少需要 sizeof(zcsr_menu) + 内部状态.
 * 返回 NULL 表示 buf 不足或配置无效. */
zcsr_menu* zcsr_menu_create(void* buf, size_t bytes, const zcsr_menu_config* cfg);

/* 销毁 (当前实现为空操作, 预留) */
void zcsr_menu_destroy(zcsr_menu* menu);

/* 处理按键. 返回:
 *   >=0: 用户确认了某项, 返回该项的 id
 *   -1:  无确认 (普通导航或按键未处理)
 *   -2:  用户按了 ESC 取消
 */
int zcsr_menu_handle_key(zcsr_menu* menu, zcsr_keycode key, zcsr_key_action action);

/* 渲染菜单到当前 glrender 帧 (必须在 zcsr_gl_begin 之后, zcsr_gl_flush 之前调用) */
void zcsr_menu_render(zcsr_menu* menu, zcsr_gl_renderer* renderer);

/* 当前选中项的索引 (0-based), -1 表示无有效项 */
int zcsr_menu_get_selected_index(const zcsr_menu* menu);

/* 当前选中项的 id, -1 表示无有效项 */
int zcsr_menu_get_selected_id(const zcsr_menu* menu);

/* 重置选择到第一个可用项 */
void zcsr_menu_reset(zcsr_menu* menu);

/* 手动设置选中项索引 (越界或禁用则忽略) */
void zcsr_menu_set_selected(zcsr_menu* menu, int index);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_MENU_UI_H */
