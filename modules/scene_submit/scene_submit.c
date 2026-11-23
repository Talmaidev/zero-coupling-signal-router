#include "zcsr/scene_submit.h"

#include "zcsr/gl_render_l2d_geo.h"

#include <float.h>
#include <math.h>

#define ZCSR_SCENE_SUBMIT_PI 3.14159265358979323846f
#define ZCSR_SCENE_SUBMIT_EPS 1e-6f

bool zcsr_scene_camera_to_camera(const zcsr_scene_camera* scene_camera,
                                 zcsr_camera* out_camera) {
    float pitch_rad;
    float sin_pitch;
    float cos_pitch;
    float ppu;
    float shear;
    if (scene_camera == NULL || out_camera == NULL) return false;
    if (scene_camera->view_w <= 0 || scene_camera->view_h <= 0) return false;
    if (!isfinite(scene_camera->target_x) ||
        !isfinite(scene_camera->target_y) ||
        !isfinite(scene_camera->target_z)) {
        return false;
    }
    if (!isfinite(scene_camera->ortho_size) ||
        scene_camera->ortho_size <= ZCSR_SCENE_SUBMIT_EPS) {
        return false;
    }
    if (!isfinite(scene_camera->pitch_deg) ||
        scene_camera->pitch_deg <= 0.0f || scene_camera->pitch_deg > 90.0f) {
        return false;
    }
    if (!isfinite(scene_camera->yaw_deg) ||
        fabsf(scene_camera->yaw_deg) > 1e-4f ||
        !isfinite(scene_camera->roll_deg) ||
        fabsf(scene_camera->roll_deg) > 1e-4f) {
        return false; /* the 2D shear camera has no yaw/roll */
    }

    pitch_rad = scene_camera->pitch_deg * (ZCSR_SCENE_SUBMIT_PI / 180.0f);
    sin_pitch = sinf(pitch_rad);
    cos_pitch = cosf(pitch_rad);
    if (sin_pitch <= ZCSR_SCENE_SUBMIT_EPS) return false;

    ppu = (float)scene_camera->view_h / (2.0f * scene_camera->ortho_size);
    if (!isfinite(ppu) || ppu <= 0.0f) return false;
    shear = cos_pitch / sin_pitch;
    if (!isfinite(shear)) return false;

    out_camera->center_x = scene_camera->target_x;
    /* Scene target (target_y is height, target_z is depth). The shear camera
     * centers the target point, so the ground-depth center shifts by k*height. */
    out_camera->center_y = scene_camera->target_z - shear * scene_camera->target_y;
    out_camera->px_per_unit = ppu;
    out_camera->view_w = scene_camera->view_w;
    out_camera->view_h = scene_camera->view_h;
    out_camera->height_shear = shear;
    return isfinite(out_camera->center_y);
}

bool zcsr_scene_entity_world_quad(const zcsr_scene* scene,
                                  zcsr_entity_id id,
                                  zcsr_scene_world_quad* out_quad) {
    const zcsr_scene_entity* entity;
    zcsr_scene_point corners[4];
    float ux, uy, uz, vx, vy, vz;
    float nx, ny, nz, nlen;
    size_t i;
    if (scene == NULL || out_quad == NULL || id == 0) return false;
    entity = zcsr_scene_get_const(scene, id);
    if (entity == NULL) return false;
    if (!zcsr_scene_entity_quad(scene, id, corners)) return false;

    for (i = 0; i < 4u; ++i) {
        if (!isfinite(corners[i].x) || !isfinite(corners[i].y) ||
            !isfinite(corners[i].z)) {
            return false;
        }
        out_quad->corners_xyz[i * 3u + 0u] = corners[i].x;
        out_quad->corners_xyz[i * 3u + 1u] = corners[i].z;
        out_quad->corners_xyz[i * 3u + 2u] = corners[i].y;
    }

    ux = corners[1].x - corners[0].x;
    uy = corners[1].y - corners[0].y;
    uz = corners[1].z - corners[0].z;
    vx = corners[3].x - corners[0].x;
    vy = corners[3].y - corners[0].y;
    vz = corners[3].z - corners[0].z;
    nx = uy * vz - uz * vy;
    ny = uz * vx - ux * vz;
    nz = ux * vy - uy * vx;
    /* Scene (x,y,z) -> Light2D (x,z,y). */
    nlen = sqrtf(nx * nx + nz * nz + ny * ny);
    if (nlen <= ZCSR_SCENE_SUBMIT_EPS) {
        out_quad->normal_xyz[0] = 0.0f;
        out_quad->normal_xyz[1] = 1.0f;
        out_quad->normal_xyz[2] = 0.0f;
    } else {
        out_quad->normal_xyz[0] = nx / nlen;
        out_quad->normal_xyz[1] = nz / nlen;
        out_quad->normal_xyz[2] = ny / nlen;
    }
    if (!isfinite(out_quad->normal_xyz[0]) ||
        !isfinite(out_quad->normal_xyz[1]) ||
        !isfinite(out_quad->normal_xyz[2])) {
        return false;
    }

    out_quad->id = entity->id;
    out_quad->tint_rgba[0] = entity->tint_r;
    out_quad->tint_rgba[1] = entity->tint_g;
    out_quad->tint_rgba[2] = entity->tint_b;
    out_quad->tint_rgba[3] = entity->tint_a;
    out_quad->texture = entity->texture;
    return true;
}

static bool scene_entity_is_drawable(const zcsr_scene* scene, size_t index) {
    zcsr_scene_point corners[4];
    const zcsr_scene_entity* entity = &scene->entities[index];
    if (!entity->visible || entity->kind == ZCSR_SCENE_KIND_LIGHT) return false;
    return zcsr_scene_entity_quad(scene, entity->id, corners);
}

/* Path-B q key. Scene (x,y,z) -> Light2D (x,z,y): the Light2D ground axis is
 * scene z, the elevation axis is scene y. */
static float scene_entity_lit_q(const zcsr_scene* scene,
                                const zcsr_camera* camera,
                                const zcsr_scene_entity* entity,
                                bool* out_valid) {
    zcsr_scene_point corners[4];
    float sum = 0.0f;
    size_t i;
    if (!zcsr_scene_entity_quad(scene, entity->id, corners)) {
        *out_valid = false;
        return FLT_MAX;
    }
    for (i = 0; i < 4u; ++i) {
        float q = camera->height_shear * (corners[i].z - camera->center_y) +
                  corners[i].y;
        if (!isfinite(q)) {
            *out_valid = false;
            return FLT_MAX;
        }
        sum += q;
    }
    *out_valid = true;
    return sum * 0.25f;
}

static int lit_entity_before(const float* q, size_t ai, size_t bi) {
    if (q[ai] != q[bi]) return q[ai] < q[bi]; /* ascending = far to near */
    return ai < bi;
}

size_t zcsr_scene_build_lit_order(const zcsr_scene* scene,
                                  const zcsr_camera* camera,
                                  size_t* out_indices,
                                  size_t capacity) {
    float q[ZCSR_SCENE_MAX_ENTITIES];
    size_t indices[ZCSR_SCENE_MAX_ENTITIES];
    size_t count = 0u;
    size_t written;
    size_t i, j;
    if (scene == NULL || camera == NULL || out_indices == NULL ||
        capacity == 0u) {
        return 0u;
    }
    if (!isfinite(camera->height_shear) || !isfinite(camera->center_y)) {
        return 0u;
    }
    for (i = 0u; i < scene->count; ++i) {
        const zcsr_scene_entity* entity = &scene->entities[i];
        bool valid = false;
        float key;
        if (!entity->visible || entity->kind == ZCSR_SCENE_KIND_LIGHT) continue;
        key = scene_entity_lit_q(scene, camera, entity, &valid);
        if (!valid) continue;
        q[i] = key;
        indices[count] = i;
        j = count;
        while (j > 0u &&
               lit_entity_before(q, indices[j], indices[j - 1u])) {
            size_t tmp = indices[j];
            indices[j] = indices[j - 1u];
            indices[j - 1u] = tmp;
            --j;
        }
        ++count;
    }
    written = (count < capacity) ? count : capacity;
    for (i = 0u; i < written; ++i) out_indices[i] = indices[i];
    return written;
}

static size_t count_visible(const zcsr_scene* scene) {
    size_t count = 0u;
    size_t i;
    for (i = 0u; i < scene->count; ++i)
        if (scene->entities[i].visible) ++count;
    return count;
}

static size_t count_drawable(const zcsr_scene* scene, const size_t* order,
                             size_t visible_count) {
    size_t count = 0u;
    size_t i;
    for (i = 0u; i < visible_count; ++i)
        if (scene_entity_is_drawable(scene, order[i])) ++count;
    return count;
}

static bool zcsr_scene_submit_lit(zcsr_gl_renderer* renderer,
                                  const zcsr_scene* scene,
                                  const zcsr_camera* camera,
                                  const size_t* order,
                                  size_t drawable,
                                  zcsr_scene_submit_stats* stats) {
    size_t i;
    bool ok;
    if (!zcsr_gl_l2d_geo_begin(renderer, camera)) return false;

    ok = true;
    for (i = 0u; i < drawable; ++i) {
        const zcsr_scene_entity* entity = &scene->entities[order[i]];
        zcsr_scene_world_quad quad;
        bool face_ok;
        if (!zcsr_scene_entity_world_quad(scene, entity->id, &quad)) {
            if (stats != NULL) ++stats->skipped;
            ok = false;
            continue;
        }
        if (quad.texture != 0u) {
            static const float uv_st[8] = {
                0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f
            };
            static const float emissive[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            face_ok = zcsr_gl_l2d_geo_tex_face(
                renderer, (zcsr_gl_texture)quad.texture,
                quad.corners_xyz, uv_st, quad.tint_rgba,
                quad.normal_xyz, emissive);
        } else {
            static const float emissive[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            face_ok = zcsr_gl_l2d_geo_face(
                renderer, quad.corners_xyz, quad.tint_rgba,
                quad.normal_xyz, emissive);
        }
        if (face_ok) {
            if (stats != NULL) ++stats->submitted;
        } else {
            if (stats != NULL) ++stats->skipped;
            ok = false;
        }
    }
    if (!zcsr_gl_l2d_geo_end(renderer)) ok = false;
    return ok;
}

bool zcsr_scene_submit(zcsr_gl_renderer* renderer,
                       const zcsr_scene* scene,
                       zcsr_scene_submit_path path,
                       zcsr_scene_quad_instance* scratch,
                       size_t capacity,
                       zcsr_scene_submit_stats* out_stats) {
    zcsr_scene_submit_stats local;
    if (out_stats != NULL) {
        out_stats->visible = 0u;
        out_stats->drawable = 0u;
        out_stats->submitted = 0u;
        out_stats->skipped = 0u;
    }
    if (renderer == NULL || scene == NULL) return false;
    if (path == ZCSR_SCENE_SUBMIT_DISPLAY) {
        size_t order[ZCSR_SCENE_MAX_ENTITIES];
        size_t visible;
        size_t drawable;
        size_t submitted;
        visible = zcsr_scene_build_order(scene, order, ZCSR_SCENE_MAX_ENTITIES);
        drawable = count_drawable(scene, order, visible);
        if (out_stats != NULL) {
            out_stats->visible = visible;
            out_stats->drawable = drawable;
        }
        if (drawable > capacity) {
            if (out_stats != NULL) out_stats->skipped = visible;
            return false;
        }
        if (drawable == 0u) return true;
        if (scratch == NULL) {
            if (out_stats != NULL) out_stats->skipped = visible;
            return false;
        }
        submitted = zcsr_scene_draw_quads(renderer, scene, scratch, capacity);
        if (out_stats != NULL) {
            out_stats->submitted = submitted;
            out_stats->skipped = visible - submitted;
        }
        return submitted == drawable;
    }
    if (path == ZCSR_SCENE_SUBMIT_LIT) {
        zcsr_camera camera;
        size_t order[ZCSR_SCENE_MAX_ENTITIES];
        size_t drawable;
        local.visible = count_visible(scene);
        local.drawable = 0u;
        local.submitted = 0u;
        local.skipped = local.visible;
        if (!zcsr_scene_camera_to_camera(&scene->camera, &camera)) {
            if (out_stats != NULL) *out_stats = local;
            return false;
        }
        drawable = zcsr_scene_build_lit_order(scene, &camera, order,
                                              ZCSR_SCENE_MAX_ENTITIES);
        local.drawable = drawable;
        if (!zcsr_scene_submit_lit(renderer, scene, &camera, order, drawable,
                                   &local)) {
            local.skipped = local.visible - local.submitted;
            if (out_stats != NULL) *out_stats = local;
            return false;
        }
        local.skipped = local.visible - local.submitted;
        if (out_stats != NULL) *out_stats = local;
        return local.submitted == local.drawable;
    }
    return false;
}
