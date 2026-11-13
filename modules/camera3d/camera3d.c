/* modules/camera3d/camera3d.c — software perspective camera.
 * Zero heap, no GL: projects points and billboards to screen pixels so the 2D
 * quad renderer can draw 3D scenes without an MVP stage. */
#include "zcsr/camera3d.h"

#include <stddef.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float deg_to_rad(float d) { return d * (float)M_PI / 180.0f; }

/* World -> view: translate, yaw around Y, then pitch around X. */
static void world_to_view(const zcsr_camera3d* c, float x, float y, float z,
                          float* vx, float* vy, float* vz) {
    float dx = x - c->eye_x;
    float dy = y - c->eye_y;
    float dz = z - c->eye_z;
    float cy = cosf(deg_to_rad(c->yaw_deg));
    float sy = sinf(deg_to_rad(c->yaw_deg));
    float cp = cosf(deg_to_rad(c->pitch_deg));
    float sp = sinf(deg_to_rad(c->pitch_deg));
    float x1, z1;

    /* yaw: rotate around +Y so that yaw=0 keeps looking towards -Z */
    x1 =  dx * cy - dz * sy;
    z1 =  dx * sy + dz * cy;

    /* pitch: positive looks down. Rotation about X by +theta maps the view axis
     * (0,-sin, -cos) onto (0,0,-1):  y' = y cp - z sp,  z' = y sp + z cp */
    *vx = x1;
    *vy = dy * cp - z1 * sp;
    *vz = dy * sp + z1 * cp;
}

float zcsr_camera3d_px_per_unit(const zcsr_camera3d* c, float depth) {
    float f;
    if (c == NULL || depth <= 0.0f) return 0.0f;
    if (c->fov_y_deg <= 1.0f || c->fov_y_deg >= 179.0f) return 0.0f;
    f = 1.0f / tanf(deg_to_rad(c->fov_y_deg) * 0.5f);
    return (float)c->view_h * 0.5f * f / depth;
}

void zcsr_camera3d_init(zcsr_camera3d* c, int view_w, int view_h,
                        float eye_x, float eye_y, float eye_z,
                        float yaw_deg, float pitch_deg, float fov_y_deg) {
    if (c == NULL) return;
    c->eye_x = eye_x;
    c->eye_y = eye_y;
    c->eye_z = eye_z;
    c->yaw_deg = yaw_deg;
    c->pitch_deg = pitch_deg;
    c->fov_y_deg = (fov_y_deg < 20.0f) ? 20.0f : (fov_y_deg > 150.0f ? 150.0f : fov_y_deg);
    c->near_z = 0.1f;
    c->far_z = 10000.0f;
    c->view_w = view_w;
    c->view_h = view_h;
}

void zcsr_camera3d_set_viewport(zcsr_camera3d* c, int w, int h) {
    if (c == NULL) return;
    c->view_w = w;
    c->view_h = h;
}

void zcsr_camera3d_look_at(zcsr_camera3d* c, float tx, float ty, float tz) {
    float dx, dy, dz, horiz, len;
    if (c == NULL) return;
    dx = tx - c->eye_x;
    dy = ty - c->eye_y;
    dz = tz - c->eye_z;
    horiz = sqrtf(dx * dx + dz * dz);
    len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len <= 0.0001f) return;
    c->yaw_deg = atan2f(dx, -dz) * 180.0f / (float)M_PI;
    c->pitch_deg = atan2f(dy, horiz) * 180.0f / (float)M_PI;
}

bool zcsr_camera3d_project(const zcsr_camera3d* c,
                           float x, float y, float z,
                           float* out_px, float* out_py, float* out_depth) {
    float vx, vy, vz, depth, f;
    if (c == NULL) return false;
    world_to_view(c, x, y, z, &vx, &vy, &vz);
    /* camera looks down -Z: visible geometry has vz < 0, depth = -vz */
    depth = -vz;
    if (depth <= c->near_z || depth > c->far_z) return false;
    f = 1.0f / tanf(deg_to_rad(c->fov_y_deg) * 0.5f);
    if (out_px) *out_px = (float)c->view_w * 0.5f + (vx * f / depth) * (float)c->view_w * 0.5f;
    if (out_py) *out_py = (float)c->view_h * 0.5f + (vy * f / depth) * (float)c->view_h * 0.5f;
    if (out_depth) *out_depth = depth;
    return true;
}

bool zcsr_camera3d_billboard(const zcsr_camera3d* c,
                             float x, float y, float z,
                             float world_w, float world_h,
                             float* out_x, float* out_y,
                             float* out_w, float* out_h) {
    float px, py, depth, k;
    if (!zcsr_camera3d_project(c, x, y, z, &px, &py, &depth)) return false;
    /* view_h * 0.5 * f / depth = pixels per world unit at this depth */
    k = (float)c->view_h * 0.5f / tanf(deg_to_rad(c->fov_y_deg) * 0.5f) / depth;
    if (out_w) *out_w = world_w * k;
    if (out_h) *out_h = world_h * k;
    if (out_x) *out_x = px - (world_w * k) * 0.5f;
    if (out_y) *out_y = py - (world_h * k) * 0.5f;
    return true;
}
