/* modules/runtime_viewport/runtime_viewport.c — Engine-Owned Runtime Viewport
 *
 * 实现 Contain Fit 等比例适配和坐标变换
 */
#include "runtime_viewport.h"
#include "zcsr/diagnostics.h"

#include <math.h>
#include <stddef.h>

void zcsr_runtime_viewport_init(zcsr_runtime_viewport* vp,
                                float ref_width, float ref_height) {
    if (vp == NULL) {
        ZCSR_LOG_ERROR("runtime_viewport", "init: vp is NULL");
        return;
    }
    if (ref_width <= 0 || ref_height <= 0) {
        ZCSR_LOG_ERROR("runtime_viewport", "init: invalid ref size %.1f x %.1f", ref_width, ref_height);
        return;
    }

    vp->config.ref_width = ref_width;
    vp->config.ref_height = ref_height;
    vp->config.fit_mode = ZCSR_FIT_CONTAIN;
    vp->config.min_width = 1.0f;
    vp->config.min_height = 1.0f;

    vp->workspace_rect.x = 0.0f;
    vp->workspace_rect.y = 0.0f;
    vp->workspace_rect.w = ref_width;
    vp->workspace_rect.h = ref_height;

    vp->content_rect = vp->workspace_rect;
    vp->scale = 1.0f;
    vp->letterbox_top = 0.0f;
    vp->letterbox_bottom = 0.0f;
    vp->pillarbox_left = 0.0f;
    vp->pillarbox_right = 0.0f;

    ZCSR_LOG_INFO("runtime_viewport", "Initialized (ref=%.0fx%.0f)", ref_width, ref_height);
}

void zcsr_runtime_viewport_set_workspace(zcsr_runtime_viewport* vp,
                                         float x, float y, float w, float h) {
    if (vp == NULL) return;
    if (w < 0 || h < 0) {
        ZCSR_LOG_WARN("runtime_viewport", "set_workspace: negative size %.1f x %.1f", w, h);
        w = 0; h = 0;
    }
    vp->workspace_rect.x = x;
    vp->workspace_rect.y = y;
    vp->workspace_rect.w = w;
    vp->workspace_rect.h = h;
    zcsr_runtime_viewport_update(vp);
}

void zcsr_runtime_viewport_update(zcsr_runtime_viewport* vp) {
    if (vp == NULL) return;

    float ws_w = vp->workspace_rect.w;
    float ws_h = vp->workspace_rect.h;
    float ref_w = vp->config.ref_width;
    float ref_h = vp->config.ref_height;

    if (ws_w <= 0 || ws_h <= 0 || ref_w <= 0 || ref_h <= 0) {
        vp->content_rect.x = vp->workspace_rect.x;
        vp->content_rect.y = vp->workspace_rect.y;
        vp->content_rect.w = 0;
        vp->content_rect.h = 0;
        vp->scale = 0;
        return;
    }

    float scale;
    if (vp->config.fit_mode == ZCSR_FIT_STRETCH) {
        /* 拉伸，不保持比例 */
        vp->content_rect.x = vp->workspace_rect.x;
        vp->content_rect.y = vp->workspace_rect.y;
        vp->content_rect.w = ws_w;
        vp->content_rect.h = ws_h;
        vp->scale = 1.0f;
        vp->letterbox_top = 0.0f;
        vp->letterbox_bottom = 0.0f;
        vp->pillarbox_left = 0.0f;
        vp->pillarbox_right = 0.0f;
        return;
    }

    /* Contain Fit：取较小的缩放比例 */
    float scale_x = ws_w / ref_w;
    float scale_y = ws_h / ref_h;

    if (vp->config.fit_mode == ZCSR_FIT_CONTAIN) {
        scale = (scale_x < scale_y) ? scale_x : scale_y;
    } else { /* ZCSR_FIT_COVER */
        scale = (scale_x > scale_y) ? scale_x : scale_y;
    }

    float content_w = ref_w * scale;
    float content_h = ref_h * scale;

    /* 居中 */
    float content_x = vp->workspace_rect.x + (ws_w - content_w) * 0.5f;
    float content_y = vp->workspace_rect.y + (ws_h - content_h) * 0.5f;

    vp->content_rect.x = content_x;
    vp->content_rect.y = content_y;
    vp->content_rect.w = content_w;
    vp->content_rect.h = content_h;
    vp->scale = scale;

    /* 计算留边 */
    vp->letterbox_top = content_y - vp->workspace_rect.y;
    vp->letterbox_bottom = vp->workspace_rect.y + ws_h - (content_y + content_h);
    vp->pillarbox_left = content_x - vp->workspace_rect.x;
    vp->pillarbox_right = vp->workspace_rect.x + ws_w - (content_x + content_w);
}

void zcsr_runtime_viewport_display_to_logic(const zcsr_runtime_viewport* vp,
                                            float display_x, float display_y,
                                            float* logic_x, float* logic_y) {
    if (vp == NULL) return;
    if (vp->scale <= 0.0001f) {
        /* Scale is zero, can't transform */
        if (logic_x != NULL) *logic_x = 0;
        if (logic_y != NULL) *logic_y = 0;
        return;
    }
    if (logic_x != NULL) {
        *logic_x = (display_x - vp->content_rect.x) / vp->scale;
    }
    if (logic_y != NULL) {
        *logic_y = (display_y - vp->content_rect.y) / vp->scale;
    }
}

void zcsr_runtime_viewport_logic_to_display(const zcsr_runtime_viewport* vp,
                                          float logic_x, float logic_y,
                                          float* display_x, float* display_y) {
    if (vp == NULL) return;
    if (display_x != NULL) {
        *display_x = vp->content_rect.x + logic_x * vp->scale;
    }
    if (display_y != NULL) {
        *display_y = vp->content_rect.y + logic_y * vp->scale;
    }
}

bool zcsr_runtime_viewport_hit_content(const zcsr_runtime_viewport* vp,
                                       float display_x, float display_y) {
    if (vp == NULL) return false;
    return display_x >= vp->content_rect.x &&
           display_x <= vp->content_rect.x + vp->content_rect.w &&
           display_y >= vp->content_rect.y &&
           display_y <= vp->content_rect.y + vp->content_rect.h;
}
