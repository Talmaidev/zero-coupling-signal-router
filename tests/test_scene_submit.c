#include "zcsr/scene_submit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char* what) {
    if (!condition) {
        printf("FAIL: %s\n", what);
        ++failures;
    }
}

static void test_camera_adapter(void) {
    zcsr_scene_camera scene_cam;
    zcsr_camera camera;

    memset(&scene_cam, 0, sizeof(scene_cam));
    scene_cam.target_x = 4.0f;
    scene_cam.target_y = 2.0f;
    scene_cam.target_z = 7.0f;
    scene_cam.pitch_deg = 45.0f;
    scene_cam.ortho_size = 5.0f;
    scene_cam.view_w = 1280;
    scene_cam.view_h = 720;
    check(zcsr_scene_camera_to_camera(&scene_cam, &camera), "45deg camera adapts");
    check(fabsf(camera.px_per_unit - 72.0f) < 1e-4f, "ppu = view_h/(2*ortho)");
    check(fabsf(camera.height_shear - 1.0f) < 1e-4f, "45deg shear = 1");
    check(camera.center_x == 4.0f, "center_x = target_x");
    check(fabsf(camera.center_y - 5.0f) < 1e-4f,
          "center_y = target_z - k*target_y");
    check(camera.view_w == 1280 && camera.view_h == 720, "viewport copied");

    scene_cam.yaw_deg = 30.0f;
    check(!zcsr_scene_camera_to_camera(&scene_cam, &camera),
          "yaw rejected");
    scene_cam.yaw_deg = 0.0f;

    scene_cam.roll_deg = 10.0f;
    check(!zcsr_scene_camera_to_camera(&scene_cam, &camera),
          "roll rejected");
    scene_cam.roll_deg = 0.0f;

    scene_cam.pitch_deg = 0.0f;
    check(!zcsr_scene_camera_to_camera(&scene_cam, &camera),
          "zero pitch rejected");
}

static void test_world_quad(void) {
    zcsr_scene scene;
    zcsr_scene_entity entity;
    zcsr_scene_world_quad quad;
    zcsr_entity_id id;

    zcsr_scene_init(&scene);
    memset(&entity, 0, sizeof(entity));
    entity.x = 10.0f;
    entity.y = 2.0f;
    entity.z = 5.0f;
    entity.w = 4.0f;
    entity.h = 3.0f;
    entity.pivot_x = 0.5f;
    entity.pivot_y = 0.5f;
    entity.tint_r = 0.8f;
    entity.tint_g = 0.5f;
    entity.tint_b = 0.2f;
    entity.tint_a = 1.0f;
    entity.visible = true;
    id = zcsr_scene_add(&scene, &entity);
    check(id != 0, "entity added");
    check(zcsr_scene_entity_world_quad(&scene, id, &quad),
          "canonical world quad");

    check(fabsf(quad.corners_xyz[0] - 8.0f) < 1e-5f, "bl x");
    check(fabsf(quad.corners_xyz[1] - 5.0f) < 1e-5f, "bl depth = scene z");
    check(fabsf(quad.corners_xyz[2] - 0.5f) < 1e-5f, "bl height = pivot base");
    check(fabsf(quad.corners_xyz[3] - 12.0f) < 1e-5f, "br x");
    check(fabsf(quad.corners_xyz[11] - 3.5f) < 1e-5f, "tl height = top");
    check(fabsf(quad.normal_xyz[1] - 1.0f) < 1e-5f,
          "billboard normal faces +Light2D-Y");
    check(quad.tint_rgba[0] == 0.8f && quad.tint_rgba[1] == 0.5f &&
          quad.tint_rgba[2] == 0.2f && quad.tint_rgba[3] == 1.0f,
          "tint copied");

    /* Rotate 90 degrees around the scene height axis: the plane now spans
     * depth, so its normal must become +/-X in Light2D axes. */
    entity.rotation_deg = 90.0f;
    entity.id = id;
    check(zcsr_scene_remove(&scene, id), "old entity removed");
    id = zcsr_scene_add(&scene, &entity);
    check(zcsr_scene_entity_world_quad(&scene, id, &quad),
          "rotated world quad");
    check(fabsf(fabsf(quad.normal_xyz[0]) - 1.0f) < 1e-5f,
          "rotated normal is X");

    check(!zcsr_scene_entity_world_quad(&scene, 0, &quad),
          "zero id rejected");
    check(!zcsr_scene_entity_world_quad(NULL, id, &quad),
          "null scene rejected");
}

static zcsr_scene_entity make_entity(float x, float y, float z, float h,
                                     int layer, int order, uint32_t kind) {
    zcsr_scene_entity e;
    memset(&e, 0, sizeof(e));
    e.x = x;
    e.y = y;
    e.z = z;
    e.w = 2.0f;
    e.h = h;
    e.pivot_x = 0.5f;
    e.pivot_y = 0.0f; /* base at y, top at y+h */
    e.layer = layer;
    e.order = order;
    e.tint_r = e.tint_g = e.tint_b = 1.0f;
    e.tint_a = 1.0f;
    e.visible = true;
    e.kind = kind;
    return e;
}

static void test_lit_order(void) {
    zcsr_scene scene;
    zcsr_scene_entity far_e, raised_e, near_e, light_e;
    zcsr_entity_id far_id, raised_id, near_id, light_id;
    zcsr_camera camera;
    size_t order[ZCSR_SCENE_MAX_ENTITIES];
    size_t count;

    zcsr_scene_init(&scene);
    zcsr_scene_camera_init(&scene.camera, 1280, 720, 0.0f, 45.0f, 0.0f,
                           20.0f, 5.0f);
    check(zcsr_scene_camera_to_camera(&scene.camera, &camera),
          "lit-order camera adapts");

    /* Same layer/order: q = shear*(scene_z - center_y) + scene_y.
     * far:    z=-6, height 0..2 -> q avg -5
     * raised: z=-6, height 0..6 -> q avg -3
     * near:   z=+3, height 0..2 -> q avg +4
     * The light must be skipped. */
    far_e = make_entity(0.0f, 0.0f, -6.0f, 2.0f, 0, 0, ZCSR_SCENE_KIND_RECT);
    raised_e = make_entity(0.0f, 0.0f, -6.0f, 6.0f, 0, 0, ZCSR_SCENE_KIND_RECT);
    near_e = make_entity(0.0f, 0.0f, 3.0f, 2.0f, 0, 0, ZCSR_SCENE_KIND_RECT);
    light_e = make_entity(0.0f, 0.0f, 0.0f, 2.0f, 0, 0, ZCSR_SCENE_KIND_LIGHT);
    far_id = zcsr_scene_add(&scene, &far_e);
    raised_id = zcsr_scene_add(&scene, &raised_e);
    near_id = zcsr_scene_add(&scene, &near_e);
    light_id = zcsr_scene_add(&scene, &light_e);
    check(far_id != 0 && raised_id != 0 && near_id != 0 && light_id != 0,
          "lit-order entities added");

    count = zcsr_scene_build_lit_order(&scene, &camera, order,
                                       ZCSR_SCENE_MAX_ENTITIES);
    check(count == 3, "lit order skips the light");
    check(scene.entities[order[0]].id == far_id, "lit order far first");
    check(scene.entities[order[1]].id == raised_id,
          "lit order raised middle by height q");
    check(scene.entities[order[2]].id == near_id, "lit order near last");

    count = zcsr_scene_build_lit_order(&scene, &camera, order, 2);
    check(count == 2, "lit order respects capacity");
    check(scene.entities[order[0]].id == far_id &&
          scene.entities[order[1]].id == raised_id,
          "lit order capacity keeps the sorted prefix");

    check(zcsr_scene_build_lit_order(NULL, &camera, order,
                                     ZCSR_SCENE_MAX_ENTITIES) == 0,
          "lit order rejects null scene");
    check(zcsr_scene_build_lit_order(&scene, NULL, order,
                                     ZCSR_SCENE_MAX_ENTITIES) == 0,
          "lit order rejects null camera");
}

static void test_lit_order_cross_layer(void) {
    zcsr_scene scene;
    zcsr_scene_entity near_top, far_low;
    zcsr_entity_id near_id, far_id;
    zcsr_camera camera;
    size_t order[ZCSR_SCENE_MAX_ENTITIES];
    size_t count;

    zcsr_scene_init(&scene);
    zcsr_scene_camera_init(&scene.camera, 1280, 720, 0.0f, 45.0f, 0.0f,
                           20.0f, 5.0f);
    check(zcsr_scene_camera_to_camera(&scene.camera, &camera),
          "cross-layer camera adapts");

    /* q = shear*(scene_z - center_y) + scene_y; at 45 deg shear=1,
     * center_y=0. near_top: z=+3, y 0..2 -> q=+4, layer 0/order 0.
     * far_low: z=-6, y 0..2 -> q=-5, layer 1/order 0.
     * A layer/order-first comparator would draw near_top first; pure q must
     * draw far_low first. This is the regression guard for Path B depth. */
    near_top = make_entity(0.0f, 0.0f, 3.0f, 2.0f, 0, 0,
                           ZCSR_SCENE_KIND_RECT);
    far_low = make_entity(0.0f, 0.0f, -6.0f, 2.0f, 1, 0,
                          ZCSR_SCENE_KIND_RECT);
    near_id = zcsr_scene_add(&scene, &near_top);
    far_id = zcsr_scene_add(&scene, &far_low);
    check(near_id != 0 && far_id != 0, "cross-layer entities added");

    count = zcsr_scene_build_lit_order(&scene, &camera, order,
                                       ZCSR_SCENE_MAX_ENTITIES);
    check(count == 2, "cross-layer order count");
    check(scene.entities[order[0]].id == far_id,
          "q outranks layer/order: far first");
    check(scene.entities[order[1]].id == near_id,
          "q outranks layer/order: near last");
}

int main(void) {
    test_camera_adapter();
    test_world_quad();
    test_lit_order();
    test_lit_order_cross_layer();
    if (failures != 0) {
        printf("%d scene_submit contract failure(s)\n", failures);
        return 1;
    }
    printf("PASS: scene_submit contract\n");
    return 0;
}
