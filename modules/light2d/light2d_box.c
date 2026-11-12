/* light2d_box.c — Light2D 长方体模块
 *
 * 可复用的长方体最小单位。在 l2d_geo_begin/end 块内向 G-buffer 提交五面
 * （前/左/右/后/顶），按 painter order 排列。调用者负责 begin/end。
 */

#include "zcsr/light2d_box.h"
#include "zcsr/gl_render.h"
#include "zcsr/gl_render_l2d_geo.h"

#include <stdlib.h>

struct zcsr_light2d_box {
    float min_x, max_x;
    float min_y, max_y;
    float h;
    float tint[4];
};

int zcsr_light2d_box_create(zcsr_light2d_box** out_box) {
    if (!out_box) return -1;
    zcsr_light2d_box* box = (zcsr_light2d_box*)calloc(1, sizeof(zcsr_light2d_box));
    if (!box) return -2;
    box->min_x = 0.0f; box->max_x = 2.0f;
    box->min_y = 0.0f; box->max_y = 2.0f;
    box->h = 2.0f;
    box->tint[0]=box->tint[1]=box->tint[2]=box->tint[3]=1.0f;
    *out_box = box;
    return 0;
}

void zcsr_light2d_box_destroy(zcsr_light2d_box* box) {
    free(box);
}

int zcsr_light2d_box_set_geometry(zcsr_light2d_box* box,
                                   float min_x, float max_x,
                                   float min_y, float max_y,
                                   float h) {
    if (!box) return -1;
    box->min_x=min_x; box->max_x=max_x;
    box->min_y=min_y; box->max_y=max_y;
    box->h = h;
    return 0;
}

int zcsr_light2d_box_set_tint(zcsr_light2d_box* box,
                               float r, float g, float b, float a) {
    if (!box) return -1;
    box->tint[0]=r; box->tint[1]=g; box->tint[2]=b; box->tint[3]=a;
    return 0;
}

int zcsr_light2d_box_get_geometry(const zcsr_light2d_box* box,
                                   float* min_x, float* max_x,
                                   float* min_y, float* max_y,
                                   float* h) {
    if (!box) return -1;
    if (min_x) *min_x = box->min_x;
    if (max_x) *max_x = box->max_x;
    if (min_y) *min_y = box->min_y;
    if (max_y) *max_y = box->max_y;
    if (h) *h = box->h;
    return 0;
}

/* 提交 box 五面到 G-buffer。必须在 zcsr_gl_l2d_geo_begin/end 之间调用。
 * 五面按 painter order：前面→左面→右面→后面→顶面。每面用 tint 乘以亮度系数
 * 保持视觉层次（前暗、侧中、后亮、顶最亮）。 */
void zcsr_light2d_box_render(zcsr_light2d_box* box, zcsr_gl_renderer* r) {
    if (!box || !r) return;
    const float emissive[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float mx = box->min_x, Mx = box->max_x;
    const float my = box->min_y, My = box->max_y;
    const float h = box->h;

    /* 前面 (y=min_y, normal -y) — 最暗 */
    {
        const float corners[12] = { mx,my,0,  Mx,my,0,  Mx,my,h,  mx,my,h };
        const float normal[3] = { 0,-1,0 };
        float albedo[4] = { box->tint[0]*0.6f, box->tint[1]*0.6f, box->tint[2]*0.6f, box->tint[3] };
        (void)zcsr_gl_l2d_geo_face(r, corners, albedo, normal, emissive);
    }
    /* 左面 (x=min_x, normal -x) */
    {
        const float corners[12] = { mx,my,0,  mx,My,0,  mx,My,h,  mx,my,h };
        const float normal[3] = { -1,0,0 };
        float albedo[4] = { box->tint[0]*0.76f, box->tint[1]*0.76f, box->tint[2]*0.76f, box->tint[3] };
        (void)zcsr_gl_l2d_geo_face(r, corners, albedo, normal, emissive);
    }
    /* 右面 (x=max_x, normal +x) */
    {
        const float corners[12] = { Mx,my,0,  Mx,My,0,  Mx,My,h,  Mx,my,h };
        const float normal[3] = { 1,0,0 };
        float albedo[4] = { box->tint[0]*0.76f, box->tint[1]*0.76f, box->tint[2]*0.76f, box->tint[3] };
        (void)zcsr_gl_l2d_geo_face(r, corners, albedo, normal, emissive);
    }
    /* 后面 (y=max_y, normal +y) */
    {
        const float corners[12] = { mx,My,0,  Mx,My,0,  Mx,My,h,  mx,My,h };
        const float normal[3] = { 0,1,0 };
        float albedo[4] = { box->tint[0]*1.04f, box->tint[1]*1.04f, box->tint[2]*1.04f, box->tint[3] };
        (void)zcsr_gl_l2d_geo_face(r, corners, albedo, normal, emissive);
    }
    /* 顶面 (z=h, normal +z) — 最亮 */
    {
        const float corners[12] = { mx,my,h,  Mx,my,h,  Mx,My,h,  mx,My,h };
        const float normal[3] = { 0,0,1 };
        float albedo[4] = { box->tint[0]*1.6f, box->tint[1]*1.6f, box->tint[2]*1.6f, box->tint[3] };
        (void)zcsr_gl_l2d_geo_face(r, corners, albedo, normal, emissive);
    }
}
