/* integration/settings_ui.h — 游戏设置面板骨架 (基于 glrender + input).
 *
 * 参考三角洲行动/LOL 等主流游戏设置界面:
 *   - 左侧设置项列表 (标签 + 控件)
 *   - 三种控件: 下拉选择 / 滑块 / 分组分隔线
 *   - 底部按钮: 应用 / 取消 / 重置默认
 *   - 修改先暂存, 点"应用"后才生效 (支持取消)
 *
 * 控件交互:
 *   Up/Down 或 W/S   — 切换设置项
 *   Left/Right 或 A/D — 修改当前项的值 (下拉切换选项, 滑块增减)
 *   Enter            — 确认 (同应用)
 *   ESC              — 取消并退出
 *
 * 用法:
 *   zcsr_setting_item items[] = {
 *     { .label="Display Mode", .type=ZCSR_CTRL_DROPDOWN,
 *       .options=(const char*[]){"Windowed","Borderless","Fullscreen"}, .option_count=3, .value=0 },
 *     { .label="Brightness", .type=ZCSR_CTRL_SLIDER, .min=0.0f, .max=100.0f, .step=5.0f, .slider_value=50.0f },
 *     { .label="--- Graphics ---", .type=ZCSR_CTRL_SEPARATOR },
 *   };
 *   zcsr_settings* s = zcsr_settings_create(buf, sizeof buf, &(zcsr_settings_config){
 *     .title="SETTINGS", .items=items, .count=3, .x=60, .y=40, .width=500, .item_height=34,
 *   });
 *   // 主循环:
 *   int action = zcsr_settings_handle_key(s, key, action);  // 1=apply, -1=cancel, 0=normal
 *   if (action == 1) { apply_settings(s); }
 *   zcsr_settings_render(s, renderer);
 */
#ifndef ZCSR_SETTINGS_UI_H
#define ZCSR_SETTINGS_UI_H

#include "zcsr/gl_render.h"
#include "zcsr/input.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 控件类型 */
typedef enum {
    ZCSR_CTRL_DROPDOWN  = 0, /* 下拉选择: options[value] */
    ZCSR_CTRL_SLIDER    = 1, /* 滑块: slider_value in [min,max] */
    ZCSR_CTRL_SEPARATOR = 2, /* 分组标题/分隔线, 不可选中 */
} zcsr_ctrl_type;

/* 单个设置项. 调用者拥有并可在应用后读取 value / slider_value. */
typedef struct {
    const char*  label;
    zcsr_ctrl_type type;

    /* --- DROPDOWN --- */
    const char** options;     /* 选项字符串数组 */
    int          option_count;
    int          value;       /* 当前已应用的值 (索引) */

    /* --- SLIDER --- */
    float min, max, step;
    float slider_value;       /* 当前已应用的值 */

    /* --- 内部暂存 (应用前的编辑值, 调用者不必初始化) --- */
    int   pending_value;
    float pending_slider;
} zcsr_setting_item;

/* 面板配置 */
typedef struct {
    const char*         title;       /* 面板标题 (NULL=无) */
    zcsr_setting_item*  items;       /* 设置项数组 */
    int                 count;       /* 项数 */
    int                 x, y;        /* 面板左上角 */
    int                 width;       /* 面板宽度 (控件右对齐到此宽度) */
    int                 item_height; /* 每项高度 (默认 34) */
    float               text_scale;  /* 文字缩放 (默认 1.5) */
} zcsr_settings_config;

typedef struct zcsr_settings zcsr_settings;

/* 返回值:
 *   1  = 用户触发"应用" (Enter 或选中 Apply 按钮)
 *  -1  = 用户触发"取消" (ESC 或选中 Cancel 按钮)
 *   0  = 普通导航/编辑, 无按钮事件
 */
int  zcsr_settings_handle_key(zcsr_settings* s, zcsr_keycode key, zcsr_key_action action);

/* 渲染面板 (必须在 zcsr_gl_begin 之后, zcsr_gl_flush 之前) */
void zcsr_settings_render(zcsr_settings* s, zcsr_gl_renderer* renderer);

/* 创建面板. buf 由调用者提供, 至少 sizeof(zcsr_settings). 返回 NULL 表示配置无效. */
zcsr_settings* zcsr_settings_create(void* buf, size_t bytes, const zcsr_settings_config* cfg);

void zcsr_settings_destroy(zcsr_settings* s);

/* 把暂存值写入正式值 (调用者在收到 apply 事件后调用, 然后读取 items[].value/slider_value) */
void zcsr_settings_apply(zcsr_settings* s);

/* 丢弃暂存值, 恢复到上次应用的状态 */
void zcsr_settings_cancel(zcsr_settings* s);

/* 重置所有项到默认值 (调用者需先把 items[].value/slider_value 设为默认, 再调用此函数同步暂存) */
void zcsr_settings_reset(zcsr_settings* s);

/* 是否有未应用的修改 */
bool zcsr_settings_is_dirty(const zcsr_settings* s);

/* 当前选中项索引 (-1=无) */
int  zcsr_settings_get_selected(const zcsr_settings* s);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_SETTINGS_UI_H */
