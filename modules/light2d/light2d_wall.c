/* light2d_wall.c — Light2D 墙模块
 *
 * 可复用的垂直墙最小单位。在 l2d_geo_begin/end 块内向 G-buffer 提交一个
 * vertical quad（normal 朝 +y，z 从 0 到 h）。
 */

#include "zcsr/light2d_wall.h"
#include "zcsr/gl_render.h"
#include "zcsr/gl_render_l2d_geo.h"

#include <stdlib.h>

struct zcsr_light2d_wall {
    float y;       /* 墙的 y 坐标 */
    float h;       /* 墙高 */
    float min_x, max_x;
    float tint[4];
};

int zcsr_light2d_wall_create(zcsr_light2d_wall** out_wall) {
    if (!out_wall) return -1;
    zcsr_light2d_wall* wall = (zcsr_light2d_wall*)calloc(1, sizeof(zcsr_light2d_wall));
    if (!wall) return -2;
    wall->y = 2.0f; wall->h = 2.0f;
    wall->min_x = -2.0f; wall->max_x = 10.0f;
    wall->tint[0]=wall->tint[1]=wall->tint[2]=wall->tint[3]=1.0f;
    *out_wall = wall;
    return 0;
}

void zcsr_light2d_wall_destroy(zcsr_light2d_wall* wall) {
    free(wall);
}

int zcsr_light2d_wall_set_geometry(zcsr_light2d_wall* wall,
                                    float y, float h,
                                    float min_x, float max_x) {
    if (!wall) return -1;
    wall->y = y; wall->h = h;
    wall->min_x = min_x; wall->max_x = max_x;
    return 0;
}

int zcsr_light2d_wall_set_tint(zcsr_light2d_wall* wall,
                                float r, float g, float b, float a) {
    if (!wall) return -1;
    wall->tint[0]=r; wall->tint[1]=g; wall->tint[2]=b; wall->tint[3]=a;
    return 0;
}

int zcsr_light2d_wall_get_geometry(const zcsr_light2d_wall* wall,
                                    float* y, float* h,
                                    float* min_x, float* max_x) {
    if (!wall) return -1;
    if (y) *y = wall->y;
    if (h) *h = wall->h;
    if (min_x) *min_x = wall->min_x;
    if (max_x) *max_x = wall->max_x;
    return 0;
}

/* 提交墙 quad 到 G-buffer。必须在 zcsr_gl_l2d_geo_begin/end 之间调用。 */
void zcsr_light2d_wall_render(zcsr_light2d_wall* wall, zcsr_gl_renderer* r) {
    if (!wall || !r) return;
    const float corners[12] = {
        wall->min_x, wall->y, 0.0f,
        wall->max_x, wall->y, 0.0f,
        wall->max_x, wall->y, wall->h,
        wall->min_x, wall->y, wall->h,
    };
    const float normal[3] = { 0.0f, 1.0f, 0.0f };
    const float emissive[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    (void)zcsr_gl_l2d_geo_face(r, corners, wall->tint, normal, emissive);
}
