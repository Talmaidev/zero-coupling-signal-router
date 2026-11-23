#include "zcsr/scene_camera.h"

#include <math.h>

#define ZCSR_SCENE_CAMERA_EPS 0.00001f

typedef struct CameraBasis {
    float right_x, right_y, right_z;
    float up_x, up_y, up_z;
    float forward_x, forward_y, forward_z;
    float eye_x, eye_y, eye_z;
    float scale;
} CameraBasis;

static float dot3(float ax, float ay, float az, float bx, float by, float bz) {
    return ax * bx + ay * by + az * bz;
}

static bool camera_basis(const zcsr_scene_camera* c, CameraBasis* b) {
    float yaw, pitch, roll;
    float cy, sy, cp, sp, cr, sr;
    if (!c || !b || c->view_w <= 0 || c->view_h <= 0 ||
        c->ortho_size <= ZCSR_SCENE_CAMERA_EPS || c->distance < 0.0f)
        return false;

    yaw = c->yaw_deg * 0.017453292519943295f;
    pitch = c->pitch_deg * 0.017453292519943295f;
    roll = c->roll_deg * 0.017453292519943295f;
    cy = cosf(yaw); sy = sinf(yaw);
    cp = cosf(pitch); sp = sinf(pitch);
    cr = cosf(roll); sr = sinf(roll);

    /* Forward points from the eye to the target. yaw=0, pitch=0 looks -Z.
     * Positive pitch puts the eye above the target and looks down. */
    b->forward_x = -sy * cp;
    b->forward_y = -sp;
    b->forward_z = -cy * cp;

    b->right_x = cy;
    b->right_y = 0.0f;
    b->right_z = -sy;

    b->up_x = b->right_y * b->forward_z - b->right_z * b->forward_y;
    b->up_y = b->right_z * b->forward_x - b->right_x * b->forward_z;
    b->up_z = b->right_x * b->forward_y - b->right_y * b->forward_x;

    /* Roll rotates the screen axes around the view axis. */
    {
        float rx = b->right_x * cr + b->up_x * sr;
        float ry = b->right_y * cr + b->up_y * sr;
        float rz = b->right_z * cr + b->up_z * sr;
        float ux = -b->right_x * sr + b->up_x * cr;
        float uy = -b->right_y * sr + b->up_y * cr;
        float uz = -b->right_z * sr + b->up_z * cr;
        b->right_x = rx; b->right_y = ry; b->right_z = rz;
        b->up_x = ux; b->up_y = uy; b->up_z = uz;
    }

    b->eye_x = c->target_x - b->forward_x * c->distance;
    b->eye_y = c->target_y - b->forward_y * c->distance;
    b->eye_z = c->target_z - b->forward_z * c->distance;
    b->scale = (float)c->view_h / (2.0f * c->ortho_size);
    return true;
}

void zcsr_scene_camera_init(zcsr_scene_camera* c, int view_w, int view_h,
                            float yaw_deg, float pitch_deg, float roll_deg,
                            float distance, float ortho_size) {
    if (!c) return;
    c->target_x = 0.0f;
    c->target_y = 0.0f;
    c->target_z = 0.0f;
    c->yaw_deg = yaw_deg;
    c->pitch_deg = pitch_deg;
    c->roll_deg = roll_deg;
    c->distance = distance;
    c->ortho_size = ortho_size;
    c->view_w = view_w;
    c->view_h = view_h;
}

void zcsr_scene_camera_set_viewport(zcsr_scene_camera* c, int w, int h) {
    if (!c) return;
    c->view_w = w;
    c->view_h = h;
}

bool zcsr_scene_camera_project(const zcsr_scene_camera* c,
                               float x, float y, float z,
                               float* out_px, float* out_py, float* out_depth) {
    CameraBasis b;
    float dx, dy, dz, cx, cy, cz, depth;
    if (!camera_basis(c, &b)) return false;
    dx = x - c->target_x;
    dy = y - c->target_y;
    dz = z - c->target_z;
    cx = dot3(dx, dy, dz, b.right_x, b.right_y, b.right_z);
    cy = dot3(dx, dy, dz, b.up_x, b.up_y, b.up_z);
    cz = dot3(dx, dy, dz, b.forward_x, b.forward_y, b.forward_z);
    depth = c->distance + cz;
    if (depth <= ZCSR_SCENE_CAMERA_EPS) return false;
    if (out_px) *out_px = (float)c->view_w * 0.5f + cx * b.scale;
    if (out_py) *out_py = (float)c->view_h * 0.5f - cy * b.scale;
    if (out_depth) *out_depth = depth;
    return true;
}

bool zcsr_scene_camera_screen_to_ground(const zcsr_scene_camera* c,
                                        float px, float py,
                                        float* out_x, float* out_z) {
    return zcsr_scene_camera_screen_to_plane(c, px, py, 0.0f, out_x, out_z);
}

bool zcsr_scene_camera_screen_to_plane(const zcsr_scene_camera* c,
                                       float px, float py, float plane_y,
                                       float* out_x, float* out_z) {
    CameraBasis b;
    float ndc_x, ndc_y, ox, oy, oz, t, wx, wy, wz;
    if (!camera_basis(c, &b) || !isfinite(plane_y)) return false;
    ndc_x = (px - (float)c->view_w * 0.5f) / b.scale;
    ndc_y = -(py - (float)c->view_h * 0.5f) / b.scale;
    ox = b.eye_x + b.right_x * ndc_x + b.up_x * ndc_y;
    oy = b.eye_y + b.right_y * ndc_x + b.up_y * ndc_y;
    oz = b.eye_z + b.right_z * ndc_x + b.up_z * ndc_y;
    if (fabsf(b.forward_y) <= ZCSR_SCENE_CAMERA_EPS) return false;
    t = (plane_y - oy) / b.forward_y;
    wx = ox + b.forward_x * t;
    wy = oy + b.forward_y * t;
    wz = oz + b.forward_z * t;
    (void)wy;
    if (out_x) *out_x = wx;
    if (out_z) *out_z = wz;
    return true;
}
