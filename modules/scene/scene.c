#include "zcsr/scene.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define SCENE_RAD (0.017453292519943295f)

static bool point_finite(zcsr_scene_point p) {
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

static zcsr_scene_quad_override* quad_find(zcsr_scene* s, zcsr_entity_id id) {
    size_t i;
    if (!s || id == 0) return NULL;
    for (i = 0; i < s->quad_count; ++i)
        if (s->quads[i].id == id) return &s->quads[i];
    return NULL;
}

static const zcsr_scene_quad_override* quad_find_const(const zcsr_scene* s,
                                                        zcsr_entity_id id) {
    size_t i;
    if (!s || id == 0) return NULL;
    for (i = 0; i < s->quad_count; ++i)
        if (s->quads[i].id == id) return &s->quads[i];
    return NULL;
}

static bool default_quad(const zcsr_scene_entity* e, zcsr_scene_point out[4]) {
    float left, right, bottom, top;
    float c, sn;
    float lx[4], ly[4];
    size_t i;
    if (!e || !out) return false;
    left = -e->pivot_x * e->w;
    right = (1.0f - e->pivot_x) * e->w;
    bottom = -e->pivot_y * e->h;
    top = (1.0f - e->pivot_y) * e->h;
    c = cosf(e->rotation_deg * SCENE_RAD);
    sn = sinf(e->rotation_deg * SCENE_RAD);
    lx[0] = left;  ly[0] = bottom;
    lx[1] = right; ly[1] = bottom;
    lx[2] = right; ly[2] = top;
    lx[3] = left;  ly[3] = top;
    for (i = 0; i < 4; ++i) {
        out[i].x = e->x + lx[i] * c;
        out[i].y = e->y + ly[i];
        out[i].z = e->z - lx[i] * sn;
        if (!point_finite(out[i])) return false;
    }
    return true;
}

static bool point_in_quad(const float px[4], const float py[4], float x, float y) {
    bool inside = false;
    size_t i, j;
    for (i = 0, j = 3; i < 4; j = i++) {
        if (((py[i] > y) != (py[j] > y)) &&
            (x < (px[j] - px[i]) * (y - py[i]) / (py[j] - py[i]) + px[i]))
            inside = !inside;
    }
    return inside;
}

/* Path A depth: average eye distance of the canonical quad. A failed projection
 * sorts as farthest so it cannot cover a valid foreground entity; the renderer
 * will skip it later. */
static float entity_camera_depth(const zcsr_scene* s, const zcsr_scene_entity* e) {
    zcsr_scene_point q[4];
    float sum = 0.0f;
    size_t i;
    if (!zcsr_scene_entity_quad(s, e->id, q)) return FLT_MAX;
    for (i = 0; i < 4; ++i) {
        float depth = 0.0f;
        if (!zcsr_scene_camera_project(&s->camera, q[i].x, q[i].y, q[i].z,
                                       NULL, NULL, &depth) ||
            !isfinite(depth)) {
            return FLT_MAX;
        }
        sum += depth;
    }
    return sum * 0.25f;
}

static int entity_before_depth(const zcsr_scene* s, const float* depth,
                               size_t ai, size_t bi) {
    const zcsr_scene_entity* a = &s->entities[ai];
    const zcsr_scene_entity* b = &s->entities[bi];
    if (a->layer != b->layer) return a->layer < b->layer;
    if (a->order != b->order) return a->order < b->order;
    if (depth[ai] != depth[bi]) return depth[ai] > depth[bi]; /* farther first */
    return ai < bi;
}

void zcsr_scene_init(zcsr_scene* s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    zcsr_scene_camera_init(&s->camera, 1, 1, 0.0f, 45.0f, 0.0f, 20.0f, 8.0f);
    s->next_id = 1;
}

zcsr_entity_id zcsr_scene_add(zcsr_scene* s, const zcsr_scene_entity* entity) {
    zcsr_scene_entity copy;
    if (!s || !entity || s->count >= ZCSR_SCENE_MAX_ENTITIES) return 0;
    copy = *entity;
    if (copy.id == 0) copy.id = s->next_id++;
    else if (copy.id >= s->next_id) s->next_id = copy.id + 1;
    if (copy.w < 0.0f) copy.w = 0.0f;
    if (copy.h < 0.0f) copy.h = 0.0f;
    if (copy.tint_a == 0.0f) copy.tint_a = 1.0f;
    s->entities[s->count] = copy;
    ++s->count;
    return copy.id;
}

bool zcsr_scene_remove(zcsr_scene* s, zcsr_entity_id id) {
    size_t i;
    if (!s || id == 0) return false;
    for (i = 0; i < s->count; ++i) {
        if (s->entities[i].id == id) {
            if (i + 1 < s->count)
                memmove(&s->entities[i], &s->entities[i + 1],
                        (s->count - i - 1) * sizeof(s->entities[0]));
            --s->count;
            (void)zcsr_scene_clear_quad(s, id);
            return true;
        }
    }
    return false;
}

zcsr_scene_entity* zcsr_scene_get(zcsr_scene* s, zcsr_entity_id id) {
    size_t i;
    if (!s || id == 0) return NULL;
    for (i = 0; i < s->count; ++i)
        if (s->entities[i].id == id) return &s->entities[i];
    return NULL;
}

const zcsr_scene_entity* zcsr_scene_get_const(const zcsr_scene* s, zcsr_entity_id id) {
    size_t i;
    if (!s || id == 0) return NULL;
    for (i = 0; i < s->count; ++i)
        if (s->entities[i].id == id) return &s->entities[i];
    return NULL;
}

bool zcsr_scene_entity_quad(const zcsr_scene* s, zcsr_entity_id id,
                            zcsr_scene_point out_quad[4]) {
    const zcsr_scene_quad_override* quad;
    const zcsr_scene_entity* e;
    if (!s || !out_quad || id == 0) return false;
    e = zcsr_scene_get_const(s, id);
    if (!e) return false;
    quad = quad_find_const(s, id);
    if (quad) {
        memcpy(out_quad, quad->corners, sizeof(quad->corners));
        return true;
    }
    return default_quad(e, out_quad);
}

bool zcsr_scene_set_quad(zcsr_scene* s, zcsr_entity_id id,
                         const zcsr_scene_point quad[4]) {
    zcsr_scene_quad_override* dst;
    size_t i;
    if (!s || !quad || id == 0 || !zcsr_scene_get_const(s, id)) return false;
    for (i = 0; i < 4; ++i)
        if (!point_finite(quad[i])) return false;
    dst = quad_find(s, id);
    if (!dst) {
        if (s->quad_count >= ZCSR_SCENE_MAX_ENTITIES) return false;
        dst = &s->quads[s->quad_count++];
        dst->id = id;
    }
    memcpy(dst->corners, quad, sizeof(dst->corners));
    return true;
}

bool zcsr_scene_has_quad(const zcsr_scene* s, zcsr_entity_id id) {
    return quad_find_const(s, id) != NULL;
}

bool zcsr_scene_clear_quad(zcsr_scene* s, zcsr_entity_id id) {
    size_t i;
    if (!s || id == 0) return false;
    for (i = 0; i < s->quad_count; ++i) {
        if (s->quads[i].id == id) {
            if (i + 1 < s->quad_count)
                memmove(&s->quads[i], &s->quads[i + 1],
                        (s->quad_count - i - 1) * sizeof(s->quads[0]));
            --s->quad_count;
            return true;
        }
    }
    return false;
}

size_t zcsr_scene_build_order(const zcsr_scene* s,
                              size_t* out_indices, size_t capacity) {
    float depth[ZCSR_SCENE_MAX_ENTITIES];
    size_t written = 0;
    size_t i, j;
    if (!s || !out_indices || capacity == 0) return 0;
    for (i = 0; i < s->count; ++i) {
        if (!s->entities[i].visible) continue;
        if (written >= capacity) return written;
        depth[i] = entity_camera_depth(s, &s->entities[i]);
        out_indices[written] = i;
        j = written;
        while (j > 0 &&
               entity_before_depth(s, depth, out_indices[j], out_indices[j - 1])) {
            size_t tmp = out_indices[j];
            out_indices[j] = out_indices[j - 1];
            out_indices[j - 1] = tmp;
            --j;
        }
        ++written;
    }
    return written;
}

zcsr_entity_id zcsr_scene_pick(const zcsr_scene* s, float px, float py) {
    size_t order[ZCSR_SCENE_MAX_ENTITIES];
    size_t count, i;
    if (!s) return 0;
    count = zcsr_scene_build_order(s, order, ZCSR_SCENE_MAX_ENTITIES);
    for (i = count; i > 0; --i) {
        const zcsr_scene_entity* e = &s->entities[order[i - 1]];
        zcsr_scene_point corners[4];
        float xs[4], ys[4];
        size_t k;
        bool ok = true;
        if (!zcsr_scene_entity_quad(s, e->id, corners)) continue;
        for (k = 0; k < 4; ++k) {
            if (!zcsr_scene_camera_project(&s->camera, corners[k].x, corners[k].y,
                                           corners[k].z, &xs[k], &ys[k], NULL)) {
                ok = false;
                break;
            }
        }
        if (ok && point_in_quad(xs, ys, px, py)) return e->id;
    }
    return 0;
}
