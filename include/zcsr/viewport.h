/* modules/viewport/viewport.h — Engine-Owned Viewport
 *
 * 负责：
 * - Workspace Available Rect → Runtime Content Rect 计算
 * - Contain Fit 等比例适配
 * - Letterbox / Pillarbox 留边
 * - 坐标正逆变换
 *
 * 不负责：
 * - 实际 GL 渲染
 * - Light2D 场景渲染
 * - Editor UI 布局
 */
#ifndef ZCSR_VIEWPORT_H
#define ZCSR_VIEWPORT_H

#include <stdbool.h>
#include "zcsr/platform.h"  /* zcsr_rectf */

#ifdef __cplusplus
extern "C" {
#endif

/* 视口适配模式 */
typedef enum zcsr_fit_mode {
    ZCSR_FIT_CONTAIN,   /* 等比例适配，居中，留边 */
    ZCSR_FIT_COVER,     /* 等比例覆盖，裁切 */
    ZCSR_FIT_STRETCH    /* 拉伸，不保持比例 */
} zcsr_fit_mode;

/* Viewport 配置 */
typedef struct zcsr_viewport_config {
    /* 项目参考分辨率（游戏 UI 设计基准） */
    float ref_width;
    float ref_height;

    /* 适配模式 */
    zcsr_fit_mode fit_mode;

    /* 最小尺寸限制 */
    float min_width;
    float min_height;
} zcsr_viewport_config;

/* Viewport 状态 */
typedef struct zcsr_viewport {
    /* 配置 */
    zcsr_viewport_config config;

    /* 输入：工作区可用矩形 */
    zcsr_rectf workspace_rect;

    /* 输出：游戏内容实际显示矩形 */
    zcsr_rectf content_rect;

    /* 缩放比例 */
    float scale;

    /* Letterbox / Pillarbox 尺寸 */
    float letterbox_top;
    float letterbox_bottom;
    float pillarbox_left;
    float pillarbox_right;
} zcsr_viewport;

/* 初始化 Viewport */
void zcsr_viewport_init(zcsr_viewport* vp,
                                float ref_width, float ref_height);

/* 设置工作区可用矩形 */
void zcsr_viewport_set_workspace(zcsr_viewport* vp,
                                         float x, float y, float w, float h);

/* 更新计算（每次工作区大小变化时调用） */
void zcsr_viewport_update(zcsr_viewport* vp);

/* 坐标变换：显示坐标 → 游戏逻辑坐标 */
void zcsr_viewport_display_to_logic(const zcsr_viewport* vp,
                                            float display_x, float display_y,
                                            float* logic_x, float* logic_y);

/* 坐标变换：游戏逻辑坐标 → 显示坐标 */
void zcsr_viewport_logic_to_display(const zcsr_viewport* vp,
                                          float logic_x, float logic_y,
                                          float* display_x, float* display_y);

/* 判断显示坐标是否在游戏内容区域内 */
bool zcsr_viewport_hit_content(const zcsr_viewport* vp,
                                       float display_x, float display_y);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_VIEWPORT_H */
