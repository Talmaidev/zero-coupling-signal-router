/* light2d_floor.h — Light2D 地板模块接口
 *
 * 可复用的地板最小单元，参数可由编辑器自由调整：
 *   - 位置：min_x / max_x / min_y / max_y
 *   - 颜色：tint
 */

#ifndef ZCSR_LIGHT2D_FLOOR_H
#define ZCSR_LIGHT2D_FLOOR_H

#include "zcsr/gl_render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zcsr_light2d_floor zcsr_light2d_floor;

/* 创建地板，返回 0 成功，负数失败 */
int zcsr_light2d_floor_create(zcsr_light2d_floor** out_floor);

/* 销毁地板 */
void zcsr_light2d_floor_destroy(zcsr_light2d_floor* floor);

/* 设置地板尺寸 */
int zcsr_light2d_floor_set_size(zcsr_light2d_floor* floor,
                                 float min_x, float max_x,
                                 float min_y, float max_y);

/* 设置地板颜色 */
int zcsr_light2d_floor_set_tint(zcsr_light2d_floor* floor,
                               float r, float g, float b, float a);

/* 获取地板参数 */
int zcsr_light2d_floor_get_size(const zcsr_light2d_floor* floor,
                                 float* min_x, float* max_x,
                                 float* min_y, float* max_y);

/* 渲染地板 */
void zcsr_light2d_floor_render(zcsr_light2d_floor* floor, zcsr_gl_renderer* r);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_LIGHT2D_FLOOR_H */
