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
 *   鼠标 — zcsr_input_ext (zcsr_mouse_state), 每帧调用 zcsr_game_settings_update_mouse
 *   键盘 — zcsr_input (zcsr_key_event), 每个事件调用 zcsr_game_settings_handle_key
 *
 * 键盘导航:
 *   Up/Down 或 W/S   — 切换设置项
 *   Left/Right 或 A/D — 修改当前项 (下拉切选项, 滑块增减, 开关切换)
 *   Enter/Space      — 展开下拉 / 切换开关 / 激活按钮
 *   ESC              — 关闭下拉 (若打开) 或 取消退出
 *   Tab              — 焦点在设置项与底部按钮间切换
 *
 * 用法:
 *   zcsr_game_settings_item video_items[] = {
 *     { .label="显示模式", .description="选择窗口显示方式", .type=ZCSR_GAME_SETTINGS_DROPDOWN,
 *       .options=(const char*[]){"窗口","无边框窗口","全屏"}, .option_count=3, .value=1 },
 *     { .label="亮度", .type=ZCSR_GAME_SETTINGS_SLIDER, .min=0,.max=100,.step=1,.slider_value=50 },
 *     { .label="垂直同步", .type=ZCSR_GAME_SETTINGS_TOGGLE, .toggle_value=true },
 *     { .label="--- 战斗视角 ---", .type=ZCSR_GAME_SETTINGS_SEPARATOR },
 *   };
 *   zcsr_game_settings_tab tabs[] = {
 *     { .name="视频", .items=video_items, .count=4 },
 *   };
 *   zcsr_game_settings* s = zcsr_game_settings_create(buf, sizeof buf, &(zcsr_game_settings_config){
 *     .tabs=tabs, .tab_count=1, .x=40, .y=20, .width=1200, .height=680,
 *   });
 *   // 主循环:
 *   zcsr_game_settings_update_mouse(s, &mouse_state);
 *   while (poll key) { int r = zcsr_game_settings_handle_key(s, key, action); if(r==1) apply(); if(r==-1) cancel(); }
 *   zcsr_game_settings_render(s, renderer);
 */
#ifndef ZCSR_GAME_SETTINGS_UI_H
#define ZCSR_GAME_SETTINGS_UI_H

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
    ZCSR_GAME_SETTINGS_DROPDOWN  = 0, /* 下拉选择: options[value], 点击展开选项列表 */
    ZCSR_GAME_SETTINGS_SLIDER    = 1, /* 滑块: slider_value in [min,max], 可拖拽 */
    ZCSR_GAME_SETTINGS_TOGGLE    = 2, /* 开关: toggle_value (true=开/false=关) */
    ZCSR_GAME_SETTINGS_SEPARATOR = 3, /* 分组标题/分隔线, 不可选中 */
} zcsr_game_settings_ctrl_type;

/* ---- 单个设置项 ----
 * 调用者拥有数组, apply 后读取 value / slider_value / toggle_value. */
typedef struct {
    const char*       label;        /* 左侧标签 */
    const char*       description;  /* 右侧信息面板说明 (NULL=无) */
    zcsr_game_settings_ctrl_type type;

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
} zcsr_game_settings_item;

/* ---- 标签页 ---- */
typedef struct {
    const char*   name;    /* 标签页名称 */
    zcsr_game_settings_item* items;   /* 该页设置项数组 */
    int           count;   /* 项数 */
} zcsr_game_settings_tab;

/* ---- 面板配置 ---- */
typedef struct {
    zcsr_game_settings_tab* tabs;        /* 标签页数组 */
    int          tab_count;   /* 标签页数 */
    int          x, y;        /* 面板左上角 */
    int          width;       /* 面板总宽度 (含右侧信息栏) */
    int          height;      /* 面板总高度 (含标签栏+底部按钮) */
    int          item_height; /* 每项高度 (默认 32) */
    float        text_scale;  /* 文字缩放 (默认 1.4) */
} zcsr_game_settings_config;

typedef struct zcsr_game_settings zcsr_game_settings; /* 不透明 */

/* 创建面板. buf 由调用者提供, 至少 sizeof(zcsr_game_settings). 返回 NULL 表示配置无效. */
zcsr_game_settings* zcsr_game_settings_create(void* buf, size_t bytes, const zcsr_game_settings_config* cfg);
void     zcsr_game_settings_destroy(zcsr_game_settings* s);

/* 运行时更新面板边界并重新计算布局. 窗口大小变化后调用,
 * 使面板自适应新窗口尺寸. w<320 或 h<200 会被钳到最小值. */
void zcsr_game_settings_set_bounds(zcsr_game_settings* s, int x, int y, int w, int h);

/* 键盘事件处理.
 * 返回值: 1=用户触发应用(调用者应执行实际设置并调用 zcsr_game_settings_apply),
 *        -1=用户触发取消(调用者应调用 zcsr_game_settings_cancel),
 *         0=普通导航/编辑 */
int  zcsr_game_settings_handle_key(zcsr_game_settings* s, zcsr_keycode key, zcsr_key_action action);

/* 鼠标状态更新 (每帧一次, 在 zcsr_input_ext_pump 之后调用).
 * 内部追踪上一帧状态以检测点击/拖拽边沿.
 * 返回值: 1=用户触发应用(调用者应执行实际设置并调用 zcsr_game_settings_apply),
 *        -1=用户触发取消(调用者应调用 zcsr_game_settings_cancel),
 *         0=普通导航/编辑 */
int  zcsr_game_settings_update_mouse(zcsr_game_settings* s, const zcsr_mouse_state* mouse);

/* 渲染面板 (必须在 zcsr_gl_begin 之后, zcsr_gl_flush 之前). */
void zcsr_game_settings_render(zcsr_game_settings* s, zcsr_gl_renderer* r);

/* 把暂存值写入正式值 (调用者在收到 apply 事件后调用). */
void zcsr_game_settings_apply(zcsr_game_settings* s);

/* 丢弃暂存值, 恢复到上次应用的状态. */
void zcsr_game_settings_cancel(zcsr_game_settings* s);

/* 重置所有项到默认值 (恢复 create 时保存的默认值到正式值和暂存值). */
void zcsr_game_settings_reset(zcsr_game_settings* s);

/* 是否有未应用的修改. */
bool zcsr_game_settings_is_dirty(const zcsr_game_settings* s);

/* 当前激活标签页索引. */
int  zcsr_game_settings_active_tab(const zcsr_game_settings* s);

/* ---- 编辑器接入：外部几何覆盖 ----
 * 页面内部每个 tab / 每个设置项 / 每个底部按钮都是一个独立 node，公式布局每帧把
 * 结果写进 node 的 local_rect。编辑器（或任何宿主）可以接管其中任意一项：
 * 传 rect 后公式布局不再覆盖它，传 NULL 把布局交还公式。
 * item 用 (tab_index, item_index) 定位，item_index 是该标签页内的序号。
 * 返回 false 表示索引越界或面板为空。 */
typedef struct { float x, y, w, h; } zcsr_game_settings_rect;

bool zcsr_game_settings_override_tab(zcsr_game_settings* s, int tab_index,
                                     const zcsr_game_settings_rect* rect);
bool zcsr_game_settings_override_item(zcsr_game_settings* s, int tab_index, int item_index,
                                      const zcsr_game_settings_rect* rect);
bool zcsr_game_settings_override_button(zcsr_game_settings* s, int button_index,
                                        const zcsr_game_settings_rect* rect);

/* 只读枚举：宿主按控件建镜像节点时需要知道有哪些控件、叫什么。 */
int         zcsr_game_settings_tab_count(const zcsr_game_settings* s);
const char* zcsr_game_settings_tab_name(const zcsr_game_settings* s, int tab_index);
int         zcsr_game_settings_item_count(const zcsr_game_settings* s, int tab_index);
const char* zcsr_game_settings_item_label(const zcsr_game_settings* s, int tab_index, int item_index);
/* 读某项已应用值（value / slider / toggle 任选其一传 NULL 表示不关心）。
 * 宿主在 Apply 之后用它把值接到自己的子系统上。 */
bool zcsr_game_settings_get_item_value(const zcsr_game_settings* s, int tab_index, int item_index,
                                       int* value, float* slider, bool* toggle);
const char* zcsr_game_settings_button_name(int button_index); /* 0=Apply 1=Cancel 2=Reset */

/* 读取控件当前矩形（几何权威在 node 里，覆盖后的值也会被读到）。 */
bool zcsr_game_settings_get_tab_rect(const zcsr_game_settings* s, int tab_index,
                                     zcsr_game_settings_rect* out);
bool zcsr_game_settings_get_item_rect(const zcsr_game_settings* s, int tab_index, int item_index,
                                      zcsr_game_settings_rect* out);
bool zcsr_game_settings_get_button_rect(const zcsr_game_settings* s, int button_index,
                                        zcsr_game_settings_rect* out);

/* ---- 布局持久化：编辑器退出编辑态时把"人改过的几何"落盘 ----
 * 只写被 override 的控件（公式位置不写，保持随窗口自适应）。
 * 文件格式是纯文本，一行一个控件：
 *   tab  <tab_index> <x> <y> <w> <h>
 *   item <tab_index> <item_index> <x> <y> <w> <h>
 *   btn  <button_index> <x> <y> <w> <h>
 * load 逐行调用对应的 override_*，所以载入后的布局立即生效；缺失文件返回 false。 */
bool zcsr_game_settings_save_overrides(const zcsr_game_settings* s, const char* path);
bool zcsr_game_settings_load_overrides(zcsr_game_settings* s, const char* path);
/* ---- 组件颜色覆盖：颜色属于组件（UIM 的最小单位），不属于编辑器 ----
 * 传 NULL 恢复主题色。值域 0..1，和渲染层的颜色约定一致。
 * save/load_overrides 会一起存盘（itemcolor / btncolor 行）。 */
typedef struct { float r, g, b; } zcsr_game_settings_color;

bool zcsr_game_settings_override_item_color(zcsr_game_settings* s, int tab_index, int item_index,
                                            const zcsr_game_settings_color* color);
bool zcsr_game_settings_override_button_color(zcsr_game_settings* s, int button_index,
                                              const zcsr_game_settings_color* color);
bool zcsr_game_settings_get_item_color(const zcsr_game_settings* s, int tab_index, int item_index,
                                       zcsr_game_settings_color* out);   /* 无覆盖时返回主题色 */
bool zcsr_game_settings_get_button_color(const zcsr_game_settings* s, int button_index,
                                         zcsr_game_settings_color* out);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_GAME_SETTINGS_UI_H */
