#include "zcsr/scene_camera.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool near(float a, float b, float eps) {
    return fabsf(a - b) <= eps;
}

int main(void) {
    zcsr_scene_camera camera;
    float px = 0.0f, py = 0.0f, depth = 0.0f;
    float gx = 0.0f, gz = 0.0f;

    zcsr_scene_camera_init(&camera, 800, 600, 0.0f, 45.0f, 0.0f, 20.0f, 8.0f);

    assert(zcsr_scene_camera_project(&camera, 0.0f, 0.0f, 0.0f, &px, &py, &depth));
    assert(near(px, 400.0f, 0.01f));
    assert(near(py, 300.0f, 0.01f));
    assert(depth > 0.0f);

    assert(zcsr_scene_camera_screen_to_ground(&camera, 400.0f, 300.0f, &gx, &gz));
    assert(near(gx, 0.0f, 0.01f));
    assert(near(gz, 0.0f, 0.01f));

    assert(zcsr_scene_camera_project(&camera, 2.0f, 0.0f, 0.0f, &px, &py, &depth));
    assert(zcsr_scene_camera_screen_to_ground(&camera, px, py, &gx, &gz));
    assert(near(gx, 2.0f, 0.35f));
    assert(near(gz, 0.0f, 0.35f));

    assert(zcsr_scene_camera_project(&camera, 2.0f, 3.0f, -1.0f, &px, &py, &depth));
    assert(zcsr_scene_camera_screen_to_plane(&camera, px, py, 3.0f, &gx, &gz));
    assert(near(gx, 2.0f, 0.35f));
    assert(near(gz, -1.0f, 0.35f));

    zcsr_scene_camera_set_viewport(&camera, 1280, 720);
    assert(zcsr_scene_camera_project(&camera, 0.0f, 0.0f, 0.0f, &px, &py, &depth));
    assert(near(px, 640.0f, 0.01f));
    assert(near(py, 360.0f, 0.01f));

    camera.yaw_deg = 90.0f;
    camera.pitch_deg = 35.0f;
    assert(zcsr_scene_camera_project(&camera, 0.0f, 0.0f, 0.0f, &px, &py, &depth));
    assert(zcsr_scene_camera_screen_to_ground(&camera, px, py, &gx, &gz));
    assert(near(gx, 0.0f, 0.6f));
    assert(near(gz, 0.0f, 0.6f));

    camera.distance = 0.0f;
    assert(zcsr_scene_camera_project(&camera, 0.0f, 0.0f, 0.0f, &px, &py, &depth) == false ||
           depth > 0.0f);
    assert(zcsr_scene_camera_screen_to_ground(&camera, 640.0f, 360.0f, &gx, &gz));

    printf("scene_camera: ok\n");
    return 0;
}
