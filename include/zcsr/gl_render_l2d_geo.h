#ifndef ZCSR_GL_RENDER_L2D_GEO_H
#define ZCSR_GL_RENDER_L2D_GEO_H
/* Public G-buffer geometry submission for the light2d deferred pipeline.
 * Hosts submit world-space convex quads (a face each); the engine's shared
 * MRT program writes all four G-buffer attachments. Hosts write NO GLSL and
 * touch NO framebuffer state.
 *
 * USAGE (inside an open light2d geometry phase — after
 * zcsr_light2d_begin_geometry, before begin_lighting):
 *   zcsr_gl_l2d_geo_begin(r, &camera);          // camera = zcsr_camera (camera.h)
 *   zcsr_gl_l2d_geo_face(r, corners, albedo, normal, emissive);  // per face
 *   zcsr_gl_l2d_geo_end(r);
 * The projection inside the program is the shared game-shear (see camera.h):
 *   screen_x = view_w/2 + (x - cam.center_x) * ppu
 *   screen_y = view_h/2 + ((y - cam.center_y) - cam.height_shear * z) * ppu
 * with ppu/k read from the camera argument and the viewport read from the
 * current GL viewport (the render target is authoritative for size).
 *
 * FACE SEMANTICS (v1):
 *   corners_xyz  4 world-space corners in boundary order, simple, convex,
 *                planar. Each is a real (x,y,z); vertical faces keep their
 *                per-pixel world z via interpolation.
 *   albedo_rgba  surface color in the SAME display (gamma) encoding as the
 *                texels: written straight to attachment 0 (RGBA8) and
 *                linearized once by the deferred composite's pow(2.2).
 *                NOT a linear-space value; >1 is clamped by RGBA8.
 *   normal_xyz   unit world normal of the whole face. v1 supports the
 *                axis-aligned classes the light shader decodes: up faces
 *                (0,0,1) and in-plane wall normals; other directions lose
 *                their z component at light time (existing pipeline limit).
 *   emissive_rgba linear HDR self-emission (attachment 3, RGBA16F). NOTE:
 *                the v1 composite multiplies (emissive + light + ambient)
 *                by albedo_lin, so emissive itself is albedo-tinted rather
 *                than an independent additive term.
 *   Internally one quad = two triangles.
 *
 * ORDERING CONTRACT (no depth attachment on the G-buffer): the host is
 * responsible for submitting opaque, non-interpenetrating faces far to
 * near. A valid sort key for the shear projection is
 *   q = camera.height_shear * (world_y - camera.center_y) + world_z
 * submitted ascending (smaller q first). world_z alone is NOT a general
 * depth; the engine performs no depth testing on this path.
 *
 * STATE: geo_begin saves program/VAO/ARRAY_BUFFER/blend/depth/cull state
 * and geo_end restores exactly what was saved. These calls never bind,
 * change or release any framebuffer or MRT draw-buffer configuration.
 * Renderer-owned resources are lazily initialized on first begin and
 * released when the renderer is destroyed. */
#include "zcsr/gl_render.h"
#include "zcsr/camera.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool zcsr_gl_l2d_geo_begin(zcsr_gl_renderer* renderer,
                           const zcsr_camera* camera);

bool zcsr_gl_l2d_geo_face(zcsr_gl_renderer* renderer,
                          const float corners_xyz[12],
                          const float albedo_rgba[4],
                          const float normal_xyz[3],
                          const float emissive_rgba[4]);

/* TEXTURED face (Q4): same G-buffer path, albedo sampled from a renderer
 * texture. texture is the PUBLIC zcsr_gl_texture handle (as returned by
 * zcsr_gl_texture_create) — no raw GLuint in the contract; the handle stays
 * host-owned (geo only binds it during the call).
 *   corners_xyz    4 world corners, same order as geo_face.
 *   uv_st          4 UV pairs (u,v per corner, SAME corner order). Convention
 *                  matches the sprite pipeline: (u0,v0) = image TOP-LEFT of
 *                  the requested sub-rectangle, v grows toward the image
 *                  bottom; corner 0 of the quad receives (u0,v0).
 *   tint_rgba      display-gamma tint multiplied with the sampled texel in
 *                  the SAME non-linear space; the product is written to
 *                  attachment 0 (RGBA8) and linearized once by the
 *                  composite pow(2.2). >1 is clamped by RGBA8.
 *   normal_xyz     face world normal (same axis-aligned classes as flat).
 *   emissive_rgba  linear HDR self-emission (NOT textured in v1; albedo-tinted
 *                  at composite time, not independently additive).
 * COLOR/ALPHA CONTRACT: textures are stored as plain GL_RGBA (no GPU sRGB
 * decode); the deferred composite's pow(2.2) is the single linearization
 * step, so the sampled texel is written to attachment 0 exactly like the
 * flat path — no double decode. ALPHA-CUTOUT v1 (explicit rule, not general
 * blending): a pixel whose sampled-alpha x tint-alpha is < 0.5 is DISCARDED
 * — it writes nothing, so the G-buffer surface submitted BENEATH it (earlier
 * in painter order) stays intact. No partial transparency, no sorting.
 * Texture alpha never becomes shadow geometry (occluders stay a separate
 * contract). */
bool zcsr_gl_l2d_geo_tex_face(zcsr_gl_renderer* renderer,
                              zcsr_gl_texture texture,
                              const float corners_xyz[12],
                              const float uv_st[8],
                              const float tint_rgba[4],
                              const float normal_xyz[3],
                              const float emissive_rgba[4]);

bool zcsr_gl_l2d_geo_end(zcsr_gl_renderer* renderer);

#ifdef __cplusplus
}
#endif
#endif /* ZCSR_GL_RENDER_L2D_GEO_H */
