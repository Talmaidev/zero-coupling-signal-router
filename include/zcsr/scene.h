#ifndef ZCSR_SCENE_H
#define ZCSR_SCENE_H

/* Fixed-capacity 2.5D scene list.
 *
 * A scene is a flat list of drawable entities with a 2.5D camera. It is not a
 * 3D scene graph, not a prefab system and not a renderer. Entities carry
 * placement, size, rotation, tint, texture handle, layer and order. The scene
 * produces a stable index order; a renderer consumes that order.
 *
 * No heap. Capacity is explicit. All ids are stable until removed. */

#include "zcsr/scene_camera.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZCSR_SCENE_MAX_ENTITIES 4096u
#define ZCSR_SCENE_MAX_LAYERS 64

typedef uint64_t zcsr_entity_id;

/* Stable entity kind. The scene list never interprets prefab_id/asset_id; it
 * only carries them so an editor, runtime loader and renderer agree on the
 * same record. 0 is the legacy procedural rect so old files remain valid. */
enum {
    ZCSR_SCENE_KIND_RECT = 0,
    ZCSR_SCENE_KIND_SPRITE = 1,
    ZCSR_SCENE_KIND_PREFAB = 2,
    ZCSR_SCENE_KIND_LIGHT = 3
};

/* One world-space point on a scene quad. The scene is 2.5D: x/z are ground
 * axes and y is height. A quad is four points in boundary order; no winding
 * convention is assumed by the scene list. */
typedef struct zcsr_scene_point {
    float x, y, z;
} zcsr_scene_point;

typedef struct zcsr_scene_entity {
    zcsr_entity_id id;
    float x, y, z;              /* world position; x/z are ground axes, y is height */
    float w, h;                 /* world-space size */
    float rotation_deg;         /* rotation in the ground plane */
    float pivot_x, pivot_y;     /* 0..1; 0.5/0.5 = center */
    int layer;
    int order;                 /* lower draws first inside a layer */
    float tint_r, tint_g, tint_b, tint_a;
    uint32_t texture;           /* renderer texture handle; 0 = untextured */
    bool visible;
    uint32_t kind;             /* ZCSR_SCENE_KIND_*; 0 = procedural rect */
    uint64_t prefab_id;        /* stable prefab source id; 0 = none */
    uint64_t asset_id;         /* stable asset id; texture is the runtime handle */
} zcsr_scene_entity;

typedef struct zcsr_scene_quad_override {
    zcsr_entity_id id;
    zcsr_scene_point corners[4];
} zcsr_scene_quad_override;

typedef struct zcsr_scene {
    zcsr_scene_camera camera;
    zcsr_scene_entity entities[ZCSR_SCENE_MAX_ENTITIES];
    zcsr_scene_quad_override quads[ZCSR_SCENE_MAX_ENTITIES];
    size_t count;
    size_t quad_count;
    uint64_t next_id;
} zcsr_scene;

void zcsr_scene_init(zcsr_scene* s);

/* Adds an entity and returns its stable id, or 0 when full/invalid. */
zcsr_entity_id zcsr_scene_add(zcsr_scene* s, const zcsr_scene_entity* entity);

bool zcsr_scene_remove(zcsr_scene* s, zcsr_entity_id id);
zcsr_scene_entity* zcsr_scene_get(zcsr_scene* s, zcsr_entity_id id);
const zcsr_scene_entity* zcsr_scene_get_const(const zcsr_scene* s, zcsr_entity_id id);

/* Write the entity's four world corners. When an explicit quad override is
 * present it is copied; otherwise a rectangle is built from x/y/z, w/h, pivot,
 * rotation and layer. The output order is bottom-left, bottom-right,
 * top-right, top-left in the rectangle's local frame. Returns false on
 * invalid input or non-finite values. */
bool zcsr_scene_entity_quad(const zcsr_scene* s, zcsr_entity_id id,
                            zcsr_scene_point out_quad[4]);

/* Replace the entity shape with an explicit world-space quad. Returns false
 * when the entity is missing or any corner is non-finite. This does not
 * rewrite x/y/z/w/h; they remain the fallback rectangle. */
bool zcsr_scene_set_quad(zcsr_scene* s, zcsr_entity_id id,
                         const zcsr_scene_point quad[4]);

/* True when the entity has an explicit quad override. */
bool zcsr_scene_has_quad(const zcsr_scene* s, zcsr_entity_id id);

/* Drop the explicit quad and return to the default rectangle shape. */
bool zcsr_scene_clear_quad(zcsr_scene* s, zcsr_entity_id id);

/* Stable path-A draw order: layer ascending, then order ascending, then
 * camera depth descending (farther first), then insertion index. Depth is the
 * average projected eye distance of the entity's canonical quad, so explicit
 * quads and elevated geometry are handled without treating y as ground depth.
 * Hidden entities are skipped. Returns the number written; 0 on invalid/full
 * output. */
size_t zcsr_scene_build_order(const zcsr_scene* s,
                              size_t* out_indices, size_t capacity);

/* Convenience finder for editor picking. Returns the topmost visible entity
 * whose projected ground rectangle contains (px,py), or 0. This is a flat
 * rectangle test; arbitrary quad picking belongs to the scene quad path. */
zcsr_entity_id zcsr_scene_pick(const zcsr_scene* s, float px, float py);

#endif /* ZCSR_SCENE_H */
