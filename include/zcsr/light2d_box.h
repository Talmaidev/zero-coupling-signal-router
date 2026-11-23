/* light2d_box.h — Light2D 长方体几何模块接口
 *
 * 这是 Light2D 的光照几何原语，不是 scene entity、不是可拖拽建筑、不是碰撞体：
 *   - 位置：min_x / max_x / min_y / max_y
 *   - 高度：h
 *   - 颜色：tint
 *
 * 它只负责把一个长方体的面提交给 Light2D G-buffer；没有贴图、材质、碰撞、
 * 文档序列化或编辑器选择语义。scene box 必须另行定义，并可复用这里的几何。 */

#ifndef ZCSR_LIGHT2D_BOX_H
#define ZCSR_LIGHT2D_BOX_H

#include "zcsr/gl_render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zcsr_light2d_box zcsr_light2d_box;

/* 创建长方体，返回 0 成功，负数失败 */
int zcsr_light2d_box_create(zcsr_light2d_box** out_box);

/* 销毁长方体 */
void zcsr_light2d_box_destroy(zcsr_light2d_box* box);

/* 设置长方体几何参数 */
int zcsr_light2d_box_set_geometry(zcsr_light2d_box* box,
                                   float min_x, float max_x,
                                   float min_y, float max_y,
                                   float h);

/* 设置长方体颜色 */
int zcsr_light2d_box_set_tint(zcsr_light2d_box* box,
                               float r, float g, float b, float a);

/* 获取长方体参数 */
int zcsr_light2d_box_get_geometry(const zcsr_light2d_box* box,
                                   float* min_x, float* max_x,
                                   float* min_y, float* max_y,
                                   float* h);

/* 渲染长方体 */
void zcsr_light2d_box_render(zcsr_light2d_box* box, zcsr_gl_renderer* r);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_LIGHT2D_BOX_H */
