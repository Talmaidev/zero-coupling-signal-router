/* integration/modern_settings_ui.h — 现代游戏设置面板 (标签页 + 鼠标 + 真正下拉框).
 *
 * 参考三角洲行动 / 英雄联盟等主流游戏设置界面:
 *   - 顶部标签页 (Tab Bar), 鼠标点击或键盘切换
 *   - 左侧设置项列表: 下拉框(可展开) / 滑块(可拖拽) / 开关(On-Off) / 分组分隔线
 *   - 右侧信息面板: 显示当前选中/悬停项的标题与说明
 *   - 底部按钮: 应用 / 取消 / 重置默认
 *   - 修改先暂存, 点"应用"后才生效 (支持取消回退)
 *
 * 输入:
 *   鼠标 — zcsr_input_ext (zcsr_mouse_state), 每帧调用 zcsr_ms_update_mouse
 *   键盘 — zcsr_input (zcsr_key_event), 每个事件调用 zcsr_ms_handle_key
 *
 * 键盘导航:
 *   Up/Down 或 W/S   — 切换设置项
 *   Left/Right 或 A/D — 修改当前项 (下拉切选项, 滑块增减, 开关切换)
 *   Enter/Space      — 展开下拉 / 切换开关 / 激活按钮
 *   ESC              — 关闭下拉 (若打开) 或 取消退出
 *   Tab              — 焦点在设置项与底部按钮间切换
 *
 * 用法:
 *   zcsr_ms_item video_items[] = {
 *     { .label="显示模式", .description="选择窗口显示方式", .type=ZCSR_MS_DROPDOWN,
 *       .options=(const char*[]){"窗口","无边框窗口","全屏"}, .option_count=3, .value=1 },
 *     { .label="亮度", .type=ZCSR_MS_SLIDER, .min=0,.max=100,.step=1,.slider_value=50 },
 *     { .label="垂直同步", .type=ZCSR_MS_TOGGLE, .toggle_value=true },
 *     { .label="--- 战斗视角 ---", .type=ZCSR_MS_SEPARATOR },
 *   };
 *   zcsr_ms_tab tabs[] = {
 *     { .name="视频", .items=video_items, .count=4 },
 *   };
 *   zcsr_ms* s = zcsr_ms_create(buf, sizeof buf, &(zcsr_ms_config){
 *     .tabs=tabs, .tab_count=1, .x=40, .y=20, .width=1200, .height=680,
 *   });
 *   // 主循环:
 *   zcsr_ms_update_mouse(s, &mouse_state);
 *   while (poll key) { int r = zcsr_ms_handle_key(s, key, action); if(r==1) apply(); if(r==-1) cancel(); }
 *   zcsr_ms_render(s, renderer);
 */
#ifndef ZCSR_MODERN_SETTINGS_UI_H
#define ZCSR_MODERN_SETTINGS_UI_H

#include "zcsr/gl_render.h"
#include "zcsr/input.h"
#include "zcsr/input_ext.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 控件类型 ---- */
typedef enum {
    ZCSR_MS_DROPDOWN  = 0, /* 下拉选择: options[value], 点击展开选项列表 */
    ZCSR_MS_SLIDER    = 1, /* 滑块: slider_value in [min,max], 可拖拽 */
    ZCSR_MS_TOGGLE    = 2, /* 开关: toggle_value (true=开/false=关) */
    ZCSR_MS_SEPARATOR = 3, /* 分组标题/分隔线, 不可选中 */
} zcsr_ms_ctrl_type;

/* ---- 单个设置项 ----
 * 调用者拥有数组, apply 后读取 value / slider_value / toggle_value. */
typedef struct {
    const char*       label;        /* 左侧标签 */
    const char*       description;  /* 右侧信息面板说明 (NULL=无) */
    zcsr_ms_ctrl_type type;

    /* --- DROPDOWN --- */
    const char** options;
    int          option_count;
    int          value;            /* 已应用值 (索引) */
    int          default_value;    /* 默认值 (create 时从 value 复制) */

    /* --- SLIDER --- */
    float min, max, step;
    float slider_value;           /* 已应用值 */
    float default_slider;         /* 默认值 (create 时从 slider_value 复制) */

    /* --- TOGGLE --- */
    bool  toggle_value;           /* 已应用值 */
    bool  default_toggle;         /* 默认值 (create 时从 toggle_value 复制) */

    /* --- 内部暂存 (调用者不必初始化) --- */
    int   pending_value;
    float pending_slider;
    bool  pending_toggle;
} zcsr_ms_item;

/* ---- 标签页 ---- */
typedef struct {
    const char*   name;    /* 标签页名称 */
    zcsr_ms_item* items;   /* 该页设置项数组 */
    int           count;   /* 项数 */
} zcsr_ms_tab;

/* ---- 面板配置 ---- */
typedef struct {
    zcsr_ms_tab* tabs;        /* 标签页数组 */
    int          tab_count;   /* 标签页数 */
    int          x, y;        /* 面板左上角 */
    int          width;       /* 面板总宽度 (含右侧信息栏) */
    int          height;      /* 面板总高度 (含标签栏+底部按钮) */
    int          item_height; /* 每项高度 (默认 32) */
    float        text_scale;  /* 文字缩放 (默认 1.4) */
} zcsr_ms_config;

typedef struct zcsr_ms zcsr_ms; /* 不透明 */

/* 创建面板. buf 由调用者提供, 至少 sizeof(zcsr_ms). 返回 NULL 表示配置无效. */
zcsr_ms* zcsr_ms_create(void* buf, size_t bytes, const zcsr_ms_config* cfg);
void     zcsr_ms_destroy(zcsr_ms* s);

/* 运行时更新面板边界并重新计算布局. 窗口大小变化后调用,
 * 使面板自适应新窗口尺寸. w<320 或 h<200 会被钳到最小值. */
void zcsr_ms_set_bounds(zcsr_ms* s, int x, int y, int w, int h);

/* 键盘事件处理.
 * 返回值: 1=用户触发应用(调用者应执行实际设置并调用 zcsr_ms_apply),
 *        -1=用户触发取消(调用者应调用 zcsr_ms_cancel),
 *         0=普通导航/编辑 */
int  zcsr_ms_handle_key(zcsr_ms* s, zcsr_keycode key, zcsr_key_action action);

/* 鼠标状态更新 (每帧一次, 在 zcsr_input_ext_pump 之后调用).
 * 内部追踪上一帧状态以检测点击/拖拽边沿.
 * 返回值: 1=用户触发应用(调用者应执行实际设置并调用 zcsr_ms_apply),
 *        -1=用户触发取消(调用者应调用 zcsr_ms_cancel),
 *         0=普通导航/编辑 */
int  zcsr_ms_update_mouse(zcsr_ms* s, const zcsr_mouse_state* mouse);

/* 渲染面板 (必须在 zcsr_gl_begin 之后, zcsr_gl_flush 之前). */
void zcsr_ms_render(zcsr_ms* s, zcsr_gl_renderer* r);

/* 把暂存值写入正式值 (调用者在收到 apply 事件后调用). */
void zcsr_ms_apply(zcsr_ms* s);

/* 丢弃暂存值, 恢复到上次应用的状态. */
void zcsr_ms_cancel(zcsr_ms* s);

/* 重置所有项到默认值 (恢复 create 时保存的默认值到正式值和暂存值). */
void zcsr_ms_reset(zcsr_ms* s);

/* 是否有未应用的修改. */
bool zcsr_ms_is_dirty(const zcsr_ms* s);

/* 当前激活标签页索引. */
int  zcsr_ms_active_tab(const zcsr_ms* s);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_MODERN_SETTINGS_UI_H */
