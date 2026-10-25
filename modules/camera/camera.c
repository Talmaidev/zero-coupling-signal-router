/* modules/camera/camera.c — 2D orthographic camera, pure float math.
 * Zero heap, no GL, no platform calls: a plain coordinate-space helper. */
#include "zcsr/camera.h"

#include <stddef.h> /* NULL */

/* Divisions go through a guarded scale so a caller-broken zoom can never
 * produce inf/NaN coordinates. */
static float zcsr_cam_safe_ppu(const zcsr_camera* c)
{
    return (c->px_per_unit > 0.0f) ? c->px_per_unit : 1.0f;
}

void zcsr_camera_init(zcsr_camera* c, int view_w, int view_h,
                      float center_x, float center_y, float px_per_unit,
                      float height_shear)
{
    if (c == NULL) return;
    c->center_x = center_x;
    c->center_y = center_y;
    c->px_per_unit = px_per_unit;
    c->view_w = view_w;
    c->view_h = view_h;
    c->height_shear = height_shear;
}

void zcsr_camera_set_viewport(zcsr_camera* c, int w, int h)
{
    if (c == NULL) return;
    c->view_w = w;
    c->view_h = h;
}

void zcsr_camera_set_zoom(zcsr_camera* c, float px_per_unit)
{
    if (c == NULL) return;
    if (px_per_unit < 8.0f) px_per_unit = 8.0f;
    if (px_per_unit > 1024.0f) px_per_unit = 1024.0f;
    c->px_per_unit = px_per_unit;
}

void zcsr_camera_move_to(zcsr_camera* c, float wx, float wy)
{
    if (c == NULL) return;
    c->center_x = wx;
    c->center_y = wy;
}

void zcsr_camera_world_to_px(const zcsr_camera* c,
                             float wx, float wy, float wz,
                             float* px, float* py)
{
    if (c == NULL) return;
    if (px != NULL) {
        *px = (wx - c->center_x) * c->px_per_unit + (float)c->view_w * 0.5f;
    }
    if (py != NULL) {
        *py = (wy - c->center_y - c->height_shear * wz) * c->px_per_unit
              + (float)c->view_h * 0.5f;
    }
}

void zcsr_camera_px_to_world(const zcsr_camera* c,
                             float px, float py, float* wx, float* wy)
{
    const float ppu = (c != NULL) ? zcsr_cam_safe_ppu(c) : 1.0f;
    if (c == NULL) return;
    if (wx != NULL) {
        *wx = (px - (float)c->view_w * 0.5f) / ppu + c->center_x;
    }
    if (wy != NULL) {
        *wy = (py - (float)c->view_h * 0.5f) / ppu + c->center_y;
    }
}

void zcsr_camera_view_rect(const zcsr_camera* c,
                           float* min_x, float* min_y,
                           float* max_x, float* max_y)
{
    float half_w;
    float half_h;
    if (c == NULL) return;
    half_w = (float)c->view_w * 0.5f / zcsr_cam_safe_ppu(c);
    half_h = (float)c->view_h * 0.5f / zcsr_cam_safe_ppu(c);
    if (min_x != NULL) *min_x = c->center_x - half_w;
    if (min_y != NULL) *min_y = c->center_y - half_h;
    if (max_x != NULL) *max_x = c->center_x + half_w;
    if (max_y != NULL) *max_y = c->center_y + half_h;
}

float zcsr_camera_world_to_px_size(const zcsr_camera* c, float world_size)
{
    if (c == NULL) return world_size;
    return world_size * c->px_per_unit;
}
