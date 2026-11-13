/* light2d_wall.h — Light2D 墙模块接口
 *
 * 可复用的墙最小单元，参数可由编辑器自由调整：
 *   - 位置：y 坐标
 *   - 高度：h
 *   - 颜色：tint
 */

#ifndef ZCSR_LIGHT2D_WALL_H
#define ZCSR_LIGHT2D_WALL_H

#include "zcsr/gl_render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zcsr_light2d_wall zcsr_light2d_wall;

/* 创建墙，返回 0 成功，负数失败 */
int zcsr_light2d_wall_create(zcsr_light2d_wall** out_wall);

/* 销毁墙 */
void zcsr_light2d_wall_destroy(zcsr_light2d_wall* wall);

/* 设置墙位置和高度 */
int zcsr_light2d_wall_set_geometry(zcsr_light2d_wall* wall,
                                    float y, float h,
                                    float min_x, float max_x);

/* 设置墙颜色 */
int zcsr_light2d_wall_set_tint(zcsr_light2d_wall* wall,
                                float r, float g, float b, float a);

/* 获取墙参数 */
int zcsr_light2d_wall_get_geometry(const zcsr_light2d_wall* wall,
                                    float* y, float* h,
                                    float* min_x, float* max_x);

/* 渲染墙 */
void zcsr_light2d_wall_render(zcsr_light2d_wall* wall, zcsr_gl_renderer* r);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_LIGHT2D_WALL_H */
