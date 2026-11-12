/* light2d_floor.c — Light2D 地板模块
 *
 * 可复用的地板最小单位。在 l2d_geo_begin/end 块内向 G-buffer 提交一个
 * up-facing quad（normal (0,0,1)）。调用者负责 begin/end 和相机设置。
 */

#include "zcsr/light2d_floor.h"
#include "zcsr/gl_render.h"
#include "zcsr/gl_render_l2d_geo.h"

#include <stdlib.h>

struct zcsr_light2d_floor {
    float min_x, max_x;
    float min_y, max_y;
    float tint[4];
};

int zcsr_light2d_floor_create(zcsr_light2d_floor** out_floor) {
    if (!out_floor) return -1;
    zcsr_light2d_floor* floor = (zcsr_light2d_floor*)calloc(1, sizeof(zcsr_light2d_floor));
    if (!floor) return -2;
    floor->min_x = -2.0f; floor->max_x = 10.0f;
    floor->min_y = -2.0f; floor->max_y = 2.0f;
    floor->tint[0] = floor->tint[1] = floor->tint[2] = floor->tint[3] = 1.0f;
    *out_floor = floor;
    return 0;
}

void zcsr_light2d_floor_destroy(zcsr_light2d_floor* floor) {
    free(floor);
}

int zcsr_light2d_floor_set_size(zcsr_light2d_floor* floor,
                                 float min_x, float max_x,
                                 float min_y, float max_y) {
    if (!floor) return -1;
    floor->min_x = min_x; floor->max_x = max_x;
    floor->min_y = min_y; floor->max_y = max_y;
    return 0;
}

int zcsr_light2d_floor_set_tint(zcsr_light2d_floor* floor,
                               float r, float g, float b, float a) {
    if (!floor) return -1;
    floor->tint[0]=r; floor->tint[1]=g; floor->tint[2]=b; floor->tint[3]=a;
    return 0;
}

int zcsr_light2d_floor_get_size(const zcsr_light2d_floor* floor,
                                 float* min_x, float* max_x,
                                 float* min_y, float* max_y) {
    if (!floor) return -1;
    if (min_x) *min_x = floor->min_x;
    if (max_x) *max_x = floor->max_x;
    if (min_y) *min_y = floor->min_y;
    if (max_y) *max_y = floor->max_y;
    return 0;
}

/* 提交地板 quad 到 G-buffer。必须在 zcsr_gl_l2d_geo_begin/end 之间调用。 */
void zcsr_light2d_floor_render(zcsr_light2d_floor* floor, zcsr_gl_renderer* r) {
    if (!floor || !r) return;
    const float corners[12] = {
        floor->min_x, floor->min_y, 0.0f,
        floor->max_x, floor->min_y, 0.0f,
        floor->max_x, floor->max_y, 0.0f,
        floor->min_x, floor->max_y, 0.0f,
    };
    const float normal[3] = { 0.0f, 0.0f, 1.0f };
    const float emissive[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    (void)zcsr_gl_l2d_geo_face(r, corners, floor->tint, normal, emissive);
}
