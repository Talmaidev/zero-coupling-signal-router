/* light2d_light.h — Light2D 点光源模块接口
 *
 * 可复用的点光源最小单元，参数可由编辑器自由调整：
 *   - 位置：x / y / z
 *   - 半径：radius
 *   - 颜色：color
 *   - 强度：intensity
 */

#ifndef ZCSR_LIGHT2D_LIGHT_H
#define ZCSR_LIGHT2D_LIGHT_H

#include "zcsr/gl_render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zcsr_light2d_light zcsr_light2d_light;

/* 创建点光源，返回 0 成功，负数失败 */
int zcsr_light2d_light_create(zcsr_light2d_light** out_light);

/* 销毁点光源 */
void zcsr_light2d_light_destroy(zcsr_light2d_light* light);

/* 设置光源位置 */
int zcsr_light2d_light_set_position(zcsr_light2d_light* light,
                                     float x, float y, float z);

/* 设置光源半径 */
int zcsr_light2d_light_set_radius(zcsr_light2d_light* light, float radius);

/* 设置光源颜色 */
int zcsr_light2d_light_set_color(zcsr_light2d_light* light,
                                  float r, float g, float b);

/* 设置光源强度 */
int zcsr_light2d_light_set_intensity(zcsr_light2d_light* light, float intensity);

/* 获取光源参数 */
int zcsr_light2d_light_get_position(const zcsr_light2d_light* light,
                                     float* x, float* y, float* z);
int zcsr_light2d_light_get_radius(const zcsr_light2d_light* light, float* radius);
int zcsr_light2d_light_get_color(const zcsr_light2d_light* light,
                                  float* r, float* g, float* b);
int zcsr_light2d_light_get_intensity(const zcsr_light2d_light* light, float* intensity);

/* 把组件参数填到 zcsr_light 结构，供 zcsr_light2d_add_light() 使用。
 * 默认 POINT 类型；需要 SPOT/PARALLEL 时调用者在 out 上直接改 type 和方向角。 */
#include "zcsr/light2d.h"
void zcsr_light2d_light_to_zcsr_light(const zcsr_light2d_light* light, zcsr_light* out);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_LIGHT2D_LIGHT_H */
