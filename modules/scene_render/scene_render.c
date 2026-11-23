#include "zcsr/scene_render.h"

#include <math.h>

size_t zcsr_scene_build_instances(const zcsr_scene* scene,
                                  zcsr_gl_instance* out, size_t capacity) {
    size_t order[ZCSR_SCENE_MAX_ENTITIES];
    size_t count, written = 0, i;
    if (!scene || !out || capacity == 0) return 0;
    count = zcsr_scene_build_order(scene, order, ZCSR_SCENE_MAX_ENTITIES);
    for (i = 0; i < count && written < capacity; ++i) {
        const zcsr_scene_entity* e = &scene->entities[order[i]];
        float cx = 0.0f, cy = 0.0f, depth = 0.0f;
        float rx = 0.0f, ry = 0.0f, rdepth = 0.0f;
        float ux = 0.0f, uy = 0.0f, udepth = 0.0f;
        float half_w, height;
        zcsr_gl_instance inst;

        /* Lights are scene data for the lighting pass, not display quads. */
        if (e->kind == ZCSR_SCENE_KIND_LIGHT) continue;
        if (!zcsr_scene_camera_project(&scene->camera, e->x, e->y, e->z, &cx, &cy, &depth))
            continue;
        if (!zcsr_scene_camera_project(&scene->camera, e->x + e->w * 0.5f, e->y, e->z,
                                       &rx, &ry, &rdepth))
            continue;
        if (!zcsr_scene_camera_project(&scene->camera, e->x, e->y, e->z + e->h,
                                       &ux, &uy, &udepth))
            continue;
        (void)ry;
        (void)rdepth;
        (void)udepth;

        half_w = fabsf(rx - cx);
        height = fabsf(uy - cy);
        inst.cx = cx;
        inst.cy = cy;
        inst.w = half_w * 2.0f;
        inst.h = height;
        inst.u0 = 0.0f;
        inst.v0 = 0.0f;
        inst.u1 = 1.0f;
        inst.v1 = 1.0f;
        inst.r = e->tint_r;
        inst.g = e->tint_g;
        inst.b = e->tint_b;
        inst.a = e->tint_a;
        inst.rotation = e->rotation_deg * 0.017453292519943295f;
        inst.amount = 0.0f;
        inst.mode = (float)ZCSR_MOD_MULTIPLY;
        inst.tex = e->texture;
        inst.z = depth;
        /* Scene textures are cutout-capable by contract: without a renderer
         * here we cannot inspect GPU metadata, so treat them as translucent.
         * Sorting in painter order is correct; pretending opaque is not. */
        inst.has_alpha = true;
        out[written++] = inst;
    }
    return written;
}

size_t zcsr_scene_build_quad_instances(const zcsr_scene* scene,
                                       zcsr_scene_quad_instance* out,
                                       size_t capacity) {
    size_t order[ZCSR_SCENE_MAX_ENTITIES];
    size_t count, written = 0, i;
    if (!scene || !out || capacity == 0) return 0;
    count = zcsr_scene_build_order(scene, order, ZCSR_SCENE_MAX_ENTITIES);
    for (i = 0; i < count && written < capacity; ++i) {
        const zcsr_scene_entity* e = &scene->entities[order[i]];
        zcsr_scene_point corners[4];
        zcsr_scene_quad_instance inst;
        float depth_sum = 0.0f;
        size_t k;
        bool ok = true;

        /* Lights are not path-A geometry. Keep DISPLAY and LIT drawable sets
         * identical so submit statistics can compare submitted == drawable. */
        if (e->kind == ZCSR_SCENE_KIND_LIGHT) continue;
        if (!zcsr_scene_entity_quad(scene, e->id, corners)) continue;
        for (k = 0; k < 4; ++k) {
            float depth = 0.0f;
            if (!zcsr_scene_camera_project(&scene->camera, corners[k].x, corners[k].y,
                                           corners[k].z, &inst.x[k], &inst.y[k], &depth)) {
                ok = false;
                break;
            }
            depth_sum += depth;
        }
        if (!ok) continue;
        inst.id = e->id;
        inst.u[0] = 0.0f; inst.v[0] = 0.0f;
        inst.u[1] = 1.0f; inst.v[1] = 0.0f;
        inst.u[2] = 1.0f; inst.v[2] = 1.0f;
        inst.u[3] = 0.0f; inst.v[3] = 1.0f;
        inst.depth = depth_sum * 0.25f;
        inst.r = e->tint_r;
        inst.g = e->tint_g;
        inst.b = e->tint_b;
        inst.a = e->tint_a;
        inst.texture = e->texture;
        out[written++] = inst;
    }
    return written;
}

size_t zcsr_scene_draw_quads(zcsr_gl_renderer* renderer, const zcsr_scene* scene,
                             zcsr_scene_quad_instance* scratch, size_t capacity) {
    size_t count, submitted = 0, i;
    if (!renderer || !scene || !scratch || capacity == 0) return 0;
    count = zcsr_scene_build_quad_instances(scene, scratch, capacity);
    if (count == 0) return 0;
    zcsr_gl_begin(renderer);
    for (i = 0; i < count; ++i) {
        zcsr_gl_quad q;
        q.tex = (zcsr_gl_texture)scratch[i].texture;
        for (int k = 0; k < 4; ++k) {
            q.x[k] = scratch[i].x[k];
            q.y[k] = scratch[i].y[k];
            q.u[k] = scratch[i].u[k];
            q.v[k] = scratch[i].v[k];
        }
        q.r = scratch[i].r;
        q.g = scratch[i].g;
        q.b = scratch[i].b;
        q.a = scratch[i].a;
        q.mode = ZCSR_MOD_MULTIPLY;
        q.amount = 0.0f;
        if (zcsr_gl_submit_quad(renderer, &q)) ++submitted;
    }
    zcsr_gl_flush(renderer);
    return submitted;
}
