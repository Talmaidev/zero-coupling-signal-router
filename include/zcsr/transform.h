#ifndef ZCSR_TRANSFORM_H
#define ZCSR_TRANSFORM_H
/* Per-entity transform component. No manager, no ECS — callers own the arrays.
 * x/y is the world-space position (and the rotation center); rotation is in
 * radians, counter-clockwise about the entity center; scale 1 = original size. */
typedef struct {
    float x, y;
    float rotation;
    float scale_x;
    float scale_y;
} zcsr_transform;

static inline void zcsr_transform_set_position(zcsr_transform* t, float x, float y)
{
    t->x = x;
    t->y = y;
}

static inline void zcsr_transform_set_rotation(zcsr_transform* t, float r)
{
    t->rotation = r;
}

static inline void zcsr_transform_set_scale(zcsr_transform* t, float s)
{
    t->scale_x = s;
    t->scale_y = s;
}

static inline void zcsr_transform_identity(zcsr_transform* t)
{
    t->x = 0.0f;
    t->y = 0.0f;
    t->rotation = 0.0f;
    t->scale_x = 1.0f;
    t->scale_y = 1.0f;
}

#endif /* ZCSR_TRANSFORM_H */
