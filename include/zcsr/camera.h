#ifndef ZCSR_CAMERA_H
#define ZCSR_CAMERA_H
/* 2D game camera: world units -> screen pixels (height-shear projection).
 * Pan + zoom + the single height-shear constant. No rotation, no perspective,
 * no matrices. Pure float math, zero heap. Screen space is y-down:
 *   px = (wx - center_x) * px_per_unit + view_w * 0.5
 *   py = (wy - center_y - height_shear * wz) * px_per_unit + view_h * 0.5
 * height_shear is the ONLY authoritative k: 0 = pure top-down, 0.5 = the
 * 2.5D debug look. Nothing else may define a second shear constant.
 * world_to_px below takes wz as the height argument (z=0 ground for the
 * classic 2D call). */
typedef struct {
    float center_x, center_y;  /* world coordinates at the viewport center */
    float px_per_unit;         /* zoom: pixels per world unit */
    int   view_w, view_h;      /* viewport size in pixels */
    float height_shear;        /* k: screen rise per world-Z unit, in PPU units */
} zcsr_camera;

void zcsr_camera_init(zcsr_camera* c, int view_w, int view_h,
                      float center_x, float center_y, float px_per_unit,
                      float height_shear);
void zcsr_camera_set_viewport(zcsr_camera* c, int w, int h);
void zcsr_camera_set_zoom(zcsr_camera* c, float px_per_unit); /* clamps to [8, 1024] */
void zcsr_camera_move_to(zcsr_camera* c, float wx, float wy);

/* world -> pixels (y-down screen), including the height-shear rise for wz.
 * For ground-plane points pass wz = 0. Out pointers may be NULL. */
void zcsr_camera_world_to_px(const zcsr_camera* c,
                             float wx, float wy, float wz,
                             float* px, float* py);
/* pixels -> world at the GROUND plane (z=0): no shear term in the inverse.
 * The light's elevation never comes from the mouse. Out pointers may be NULL. */
void zcsr_camera_px_to_world(const zcsr_camera* c,
                             float px, float py, float* wx, float* wy);

/* Viewport AABB in world coordinates (useful as a culling view rect). */
void zcsr_camera_view_rect(const zcsr_camera* c,
                           float* min_x, float* min_y,
                           float* max_x, float* max_y);

/* World length -> pixel length (scales sprite world sizes to screen). */
float zcsr_camera_world_to_px_size(const zcsr_camera* c, float world_size);

#endif /* ZCSR_CAMERA_H */
