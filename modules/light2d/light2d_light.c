/* light2d_light.c — Light2D 点光源模块
 *
 * 可复用的点光源最小单位。纯参数容器：位置/半径/颜色/强度。
 * 调用者用 zcsr_light2d_light_to_zcsr_light() 把它转成 zcsr_light 结构，
 * 再 zcsr_light2d_add_light(ctx, &light) 加到光照阶段。
 */

#include "zcsr/light2d_light.h"
#include "zcsr/gl_render.h"
#include "zcsr/light2d.h"

#include <stdlib.h>
#include <string.h>

struct zcsr_light2d_light {
    float x, y, z;
    float radius;
    float color[3];
    float intensity;
};

int zcsr_light2d_light_create(zcsr_light2d_light** out_light) {
    if (!out_light) return -1;
    zcsr_light2d_light* light = (zcsr_light2d_light*)calloc(1, sizeof(zcsr_light2d_light));
    if (!light) return -2;
    light->x = 0.0f; light->y = 0.0f; light->z = 3.0f;
    light->radius = 14.0f;
    light->color[0] = 1.0f; light->color[1] = 0.9f; light->color[2] = 0.7f;
    light->intensity = 1.0f;
    *out_light = light;
    return 0;
}

void zcsr_light2d_light_destroy(zcsr_light2d_light* light) {
    free(light);
}

int zcsr_light2d_light_set_position(zcsr_light2d_light* light, float x, float y, float z) {
    if (!light) return -1;
    light->x = x; light->y = y; light->z = z;
    return 0;
}

int zcsr_light2d_light_set_radius(zcsr_light2d_light* light, float radius) {
    if (!light) return -1;
    light->radius = radius;
    return 0;
}

int zcsr_light2d_light_set_color(zcsr_light2d_light* light, float r, float g, float b) {
    if (!light) return -1;
    light->color[0]=r; light->color[1]=g; light->color[2]=b;
    return 0;
}

int zcsr_light2d_light_set_intensity(zcsr_light2d_light* light, float intensity) {
    if (!light) return -1;
    light->intensity = intensity;
    return 0;
}

int zcsr_light2d_light_get_position(const zcsr_light2d_light* light,
                                     float* x, float* y, float* z) {
    if (!light) return -1;
    if (x) *x = light->x;
    if (y) *y = light->y;
    if (z) *z = light->z;
    return 0;
}

int zcsr_light2d_light_get_radius(const zcsr_light2d_light* light, float* radius) {
    if (!light) return -1;
    if (radius) *radius = light->radius;
    return 0;
}

int zcsr_light2d_light_get_color(const zcsr_light2d_light* light,
                                  float* r, float* g, float* b) {
    if (!light) return -1;
    if (r) *r = light->color[0];
    if (g) *g = light->color[1];
    if (b) *b = light->color[2];
    return 0;
}

int zcsr_light2d_light_get_intensity(const zcsr_light2d_light* light, float* intensity) {
    if (!light) return -1;
    if (intensity) *intensity = light->intensity;
    return 0;
}

/* 把组件参数填到 zcsr_light 结构，供 zcsr_light2d_add_light() 使用。
 * 默认 POINT 类型；需要 SPOT/PARALLEL 时调用者在 out 上直接改 type 和方向角。 */
void zcsr_light2d_light_to_zcsr_light(const zcsr_light2d_light* light, zcsr_light* out) {
    if (!light || !out) return;
    memset(out, 0, sizeof(*out));
    out->x = light->x;
    out->y = light->y;
    out->height = light->z;
    out->radius = light->radius;
    out->decay = 0.5f;
    out->type = ZCSR_LIGHT_POINT;
    out->r = light->color[0];
    out->g = light->color[1];
    out->b = light->color[2];
    out->a = light->intensity;
}
