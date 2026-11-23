#include "zcsr/scene.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static zcsr_scene_entity entity(float x, float y, int layer, int order) {
    zcsr_scene_entity e;
    e.id = 0;
    e.x = x;
    e.y = y;
    e.z = 0.0f;
    e.w = 2.0f;
    e.h = 2.0f;
    e.rotation_deg = 0.0f;
    e.pivot_x = 0.5f;
    e.pivot_y = 0.5f;
    e.layer = layer;
    e.order = order;
    e.tint_r = e.tint_g = e.tint_b = 1.0f;
    e.tint_a = 1.0f;
    e.texture = 0;
    e.visible = true;
    return e;
}

int main(void) {
    zcsr_scene scene;
    size_t order[ZCSR_SCENE_MAX_ENTITIES];
    size_t n;
    zcsr_entity_id a, b, c;
    zcsr_scene_entity ea, eb, ec;

    zcsr_scene_init(&scene);
    zcsr_scene_camera_init(&scene.camera, 800, 600, 0.0f, 45.0f, 0.0f, 20.0f, 8.0f);

    ea = entity(-2.0f, 0.0f, 0, 0);
    eb = entity(0.0f, 0.0f, 1, 0);
    ec = entity(0.0f, 1.0f, 0, 2);
    a = zcsr_scene_add(&scene, &ea);
    b = zcsr_scene_add(&scene, &eb);
    c = zcsr_scene_add(&scene, &ec);
    assert(a != 0 && b != 0 && c != 0);
    assert(a != b && b != c);

    n = zcsr_scene_build_order(&scene, order, ZCSR_SCENE_MAX_ENTITIES);
    assert(n == 3);
    assert(scene.entities[order[0]].id == a);
    assert(scene.entities[order[1]].id == c);
    assert(scene.entities[order[2]].id == b);

    assert(zcsr_scene_pick(&scene, 400.0f, 300.0f) == b);
    assert(zcsr_scene_pick(&scene, 1000.0f, 1000.0f) == 0);

    {
        zcsr_scene_point quad[4] = {
            {5.0f, 0.0f, 0.0f},
            {6.0f, 0.0f, 0.0f},
            {6.0f, 1.0f, 0.0f},
            {5.0f, 1.0f, 0.0f},
        };
        zcsr_scene_point readback[4];
        float sx = 0.0f, sy = 0.0f;
        assert(zcsr_scene_set_quad(&scene, c, quad));
        assert(zcsr_scene_has_quad(&scene, c));
        assert(zcsr_scene_entity_quad(&scene, c, readback));
        assert(readback[0].x == 5.0f && readback[2].y == 1.0f);
        assert(zcsr_scene_camera_project(&scene.camera, 5.5f, 0.5f, 0.0f, &sx, &sy, NULL));
        assert(zcsr_scene_pick(&scene, sx, sy) == c);
        assert(zcsr_scene_clear_quad(&scene, c));
        assert(!zcsr_scene_has_quad(&scene, c));
        assert(!zcsr_scene_entity_quad(&scene, c, readback) ||
               readback[0].x != 5.0f);
    }

    assert(zcsr_scene_get(&scene, b) != NULL);
    assert(zcsr_scene_remove(&scene, b));
    assert(zcsr_scene_get(&scene, b) == NULL);
    assert(zcsr_scene_build_order(&scene, order, ZCSR_SCENE_MAX_ENTITIES) == 2);

    /* Path-A ordering: within the same layer/order, camera depth decides.
     * Scene y is HEIGHT, not ground depth. The camera sits above +Z, so
     * z=-6 is farther than z=+3, and raising an entity at z=-6 moves it
     * closer to the eye without changing its ground depth. */
    {
        zcsr_scene* depth_scene = &scene;
        zcsr_scene_entity far_e, near_e, raised_e;
        zcsr_entity_id far_id, near_id, raised_id;
        size_t dorder[ZCSR_SCENE_MAX_ENTITIES];
        size_t dn;

        zcsr_scene_init(depth_scene);
        zcsr_scene_camera_init(&depth_scene->camera, 800, 600, 0.0f, 45.0f,
                               0.0f, 20.0f, 8.0f);
        far_e = entity(0.0f, 0.0f, 0, 0);
        far_e.z = -6.0f;
        near_e = entity(0.0f, 0.0f, 0, 0);
        near_e.z = 3.0f;
        raised_e = entity(0.0f, 4.0f, 0, 0);
        raised_e.z = -6.0f;
        raised_e.h = 4.0f;
        raised_e.pivot_y = 0.0f;
        far_id = zcsr_scene_add(depth_scene, &far_e);
        near_id = zcsr_scene_add(depth_scene, &near_e);
        raised_id = zcsr_scene_add(depth_scene, &raised_e);
        assert(far_id != 0 && near_id != 0 && raised_id != 0);
        dn = zcsr_scene_build_order(depth_scene, dorder,
                                    ZCSR_SCENE_MAX_ENTITIES);
        assert(dn == 3);
        assert(depth_scene->entities[dorder[0]].id == far_id);
        assert(depth_scene->entities[dorder[1]].id == raised_id);
        assert(depth_scene->entities[dorder[2]].id == near_id);
    }

    printf("scene: ok\n");
    return 0;
}
