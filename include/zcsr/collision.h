#ifndef ZCSR_COLLISION_H
#define ZCSR_COLLISION_H
/* zcsr_collision — 2D 碰撞几何原语。
 * 纯函数、无状态、无堆分配；所有输入和输出都由调用方持有。
 * 这里不包含刚体、重力、接触求解或任何游戏规则。 */
#include <stdbool.h>

typedef struct { float x, y, w, h; } zcsr_aabb; /* x,y = top-left; w,h = size */

/* 二维射线。direction 不需要归一化；maximum_distance 与 direction 使用同一单位。
 * 命中距离以 direction 的倍率写入 out_distance。 */
typedef struct {
    float origin_x;
    float origin_y;
    float direction_x;
    float direction_y;
    float maximum_distance;
} zcsr_ray;

/* 二维圆。center 是圆心，radius 必须大于 0。 */
typedef struct {
    float center_x;
    float center_y;
    float radius;
} zcsr_circle;

/* 两个有效 AABB 是否重叠；零尺寸、负尺寸、非有限值返回 false。
 * 边界接触不算重叠，点包含使用 [x, x + w) / [y, y + h) 语义。 */
bool zcsr_aabb_intersect(zcsr_aabb a, zcsr_aabb b);
bool zcsr_aabb_contains_point(zcsr_aabb a, float px, float py);

/* 射线与 AABB 的第一次相交。起点在盒内时返回 true，距离为 0。
 * out_distance 可为 NULL；失败时不会写入。 */
bool zcsr_aabb_ray_intersect(zcsr_aabb box, zcsr_ray ray, float* out_distance);

/* 圆与 AABB 是否相交。圆边界接触算相交；半径小于等于 0 返回 false。 */
bool zcsr_circle_aabb_intersect(zcsr_circle circle, zcsr_aabb box);

/* 运动 AABB 与静止 AABB 的连续扫掠检测。
 * velocity 是整段归一化时间内的位移；命中时间写入 out_time，范围 [0, 1]。
 * 起始已经重叠返回 true，out_time = 0；零速度且不重叠返回 false。 */
bool zcsr_aabb_sweep(zcsr_aabb moving_box, float velocity_x, float velocity_y,
                     zcsr_aabb fixed_box, float* out_time);

#endif /* ZCSR_COLLISION_H */
