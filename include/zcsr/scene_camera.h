#ifndef ZCSR_SCENE_CAMERA_H
#define ZCSR_SCENE_CAMERA_H

/* 2.5D scene camera.
 *
 * This is the scene camera contract: a look-at target, yaw/pitch/roll,
 * orthographic size and viewport. It projects world points to screen pixels
 * and resolves screen pixels back to the y=0 ground plane.
 *
 * It is not a 3D scene, not a 3D renderer, not a model pipeline and not
 * zcsr_camera3d. zcsr_camera3d remains a perspective projection utility.
 * Pure float math, zero heap, C11. */

#include <stdbool.h>

typedef struct zcsr_scene_camera {
    float target_x, target_y, target_z;
    float yaw_deg;       /* rotation around +Y; 0 looks toward -Z */
    float pitch_deg;     /* positive = camera above target, looking down */
    float roll_deg;      /* rotation around the view axis */
    float distance;      /* eye distance behind the target */
    float ortho_size;    /* half of the visible world height */
    int view_w, view_h;
} zcsr_scene_camera;

void zcsr_scene_camera_init(zcsr_scene_camera* c, int view_w, int view_h,
                            float yaw_deg, float pitch_deg, float roll_deg,
                            float distance, float ortho_size);

void zcsr_scene_camera_set_viewport(zcsr_scene_camera* c, int w, int h);

/* World (x,y,z) -> screen pixels. Returns false when the point is outside the
 * near/far depth slice or the camera is invalid. out_depth receives the
 * forward-axis distance from the eye. Any out pointer may be NULL. */
bool zcsr_scene_camera_project(const zcsr_scene_camera* c,
                               float x, float y, float z,
                               float* out_px, float* out_py, float* out_depth);

/* Screen pixel -> y=0 ground point. Returns false when the ray is parallel to
 * the ground plane or the camera is invalid. out_x/out_z may be NULL. */
bool zcsr_scene_camera_screen_to_ground(const zcsr_scene_camera* c,
                                        float px, float py,
                                        float* out_x, float* out_z);

/* Screen pixel -> horizontal plane at world height `plane_y`. This is the
 * arbitrary-quad handle path: each corner can be dragged on its own height
 * plane without assuming the ground. out_x/out_z may be NULL. */
bool zcsr_scene_camera_screen_to_plane(const zcsr_scene_camera* c,
                                       float px, float py, float plane_y,
                                       float* out_x, float* out_z);

#endif /* ZCSR_SCENE_CAMERA_H */
