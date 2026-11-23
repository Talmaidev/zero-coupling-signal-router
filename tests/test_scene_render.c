#include "zcsr/scene_render.h"

#include <assert.h>
#include <stdio.h>

static zcsr_scene_entity make_entity(float x, int layer, int order, float r) {
    zcsr_scene_entity e;
    e.id = 0;
    e.x = x;
    e.y = 0.0f;
    e.z = 0.0f;
    e.w = 2.0f;
    e.h = 2.0f;
    e.rotation_deg = 0.0f;
    e.pivot_x = 0.5f;
    e.pivot_y = 0.5f;
    e.layer = layer;
    e.order = order;
    e.tint_r = r;
    e.tint_g = 1.0f;
    e.tint_b = 1.0f;
    e.tint_a = 1.0f;
    e.texture = 7u;
    e.visible = true;
    return e;
}

int main(void) {
    zcsr_scene scene;
    zcsr_scene_entity back, front;
    zcsr_gl_instance out[4];
    zcsr_scene_quad_instance quads[4];
    size_t n;

    zcsr_scene_init(&scene);
    zcsr_scene_camera_init(&scene.camera, 800, 600, 0.0f, 45.0f, 0.0f, 20.0f, 8.0f);

    back = make_entity(-2.0f, 0, 0, 0.8f);
    front = make_entity(1.0f, 1, 0, 1.0f);
    assert(zcsr_scene_add(&scene, &back) != 0);
    assert(zcsr_scene_add(&scene, &front) != 0);

    n = zcsr_scene_build_instances(&scene, out, 4);
    assert(n == 2);
    assert(out[0].cx < out[1].cx);
    assert(out[0].w > 0.0f && out[0].h > 0.0f);
    assert(out[0].tex == 7u);
    assert(out[1].tex == 7u);
    assert(out[0].r < out[1].r);

    n = zcsr_scene_build_quad_instances(&scene, quads, 4);
    assert(n == 2);
    assert(quads[0].id != 0 && quads[0].id != quads[1].id);
    assert(quads[0].x[1] > quads[0].x[0]);
    assert(quads[0].y[2] < quads[0].y[0]);
    assert(quads[0].u[1] == 1.0f && quads[0].v[2] == 1.0f);
    assert(quads[0].texture == 7u);

    scene.entities[1].visible = false;
    n = zcsr_scene_build_instances(&scene, out, 4);
    assert(n == 1);

    assert(zcsr_scene_draw_quads(NULL, &scene, quads, 4) == 0);

    printf("scene_render: ok\n");
    return 0;
}
