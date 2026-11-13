#ifndef ZCSR_CAMERA3D_H
#define ZCSR_CAMERA3D_H
/* Perspective camera: 3D world -> 2D screen pixels, done in software.
 *
 * Why software: the renderer is a 2D quad batcher with no model/view/projection
 * stage, so a perspective camera cannot be expressed as a matrix upload. Instead
 * this module projects each 3D point (and each billboard's corners) itself and
 * feeds the result to the existing 2D pipeline. That keeps the renderer untouched
 * and still gives real perspective (depth scaling, off-axis vanishing points).
 *
 * Right-handed, camera looks down its own -Z. yaw rotates around +Y, pitch around
 * the camera's X. Angles in degrees. Zero heap, pure float math.
 *
 * Convention matches zcsr_camera (2D): screen space is y-down, origin top-left. */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float eye_x, eye_y, eye_z;   /* camera position in world space */
    float yaw_deg;               /* 0 = looking towards -Z */
    float pitch_deg;             /* + = looking down */
    float fov_y_deg;             /* vertical field of view */
    float near_z, far_z;         /* clip distances along view axis */
    int   view_w, view_h;        /* viewport in pixels */
} zcsr_camera3d;

void zcsr_camera3d_init(zcsr_camera3d* c, int view_w, int view_h,
                        float eye_x, float eye_y, float eye_z,
                        float yaw_deg, float pitch_deg, float fov_y_deg);

void zcsr_camera3d_set_viewport(zcsr_camera3d* c, int w, int h);
void zcsr_camera3d_look_at(zcsr_camera3d* c, float tx, float ty, float tz);

/* Project a world point. Returns false when the point is behind the camera or
 * outside [near_z, far_z]; out_depth receives the view-space distance. Any out
 * pointer may be NULL. */
bool zcsr_camera3d_project(const zcsr_camera3d* c,
                           float x, float y, float z,
                           float* out_px, float* out_py, float* out_depth);

/* Screen-space rect of a camera-facing billboard of world size `world_w` x `world_h`
 * centred at (x,y,z). Perspective shrinks it with distance. */
bool zcsr_camera3d_billboard(const zcsr_camera3d* c,
                             float x, float y, float z,
                             float world_w, float world_h,
                             float* out_x, float* out_y,
                             float* out_w, float* out_h);

/* Pixels per world unit at the given view-space depth (0 when depth <= near). */
float zcsr_camera3d_px_per_unit(const zcsr_camera3d* c, float depth);

#endif /* ZCSR_CAMERA3D_H */
