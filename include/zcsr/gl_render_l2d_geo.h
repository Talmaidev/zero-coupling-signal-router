#ifndef ZCSR_GL_RENDER_L2D_GEO_H
#define ZCSR_GL_RENDER_L2D_GEO_H
/* Public G-buffer geometry submission for the light2d deferred pipeline.
 * Hosts submit world-space convex quads (a face each); the engine's shared
 * MRT program writes all four G-buffer attachments. Hosts write NO GLSL and
 * touch NO framebuffer state.
 *
 * Boundary: this is the Light2D deferred geometry phase, not a general forward
 * 2.5D scene renderer. Projection is fixed to zcsr_camera height-shear, there
 * is no depth attachment, and the host must order faces far-to-near itself.
 * A scene quad path must be defined separately; do not overload this API.
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
 *   albedo_rgba  surface color in display (gamma) encoding. The flat entry
 *                decodes it on the CPU at submission, because attachment 0
 *                (G0) is linear RGBA16F. No sRGB decode happens at composite
 *                sample time. NOT a linear-space value; >1 is clamped to 1.
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
 * geo_begin queries the current framebuffer and FAILS unless it is the
 * renderer's exact four-attachment Light2D G-buffer (see
 * zcsr_gl_l2d_gbuffer_is_bound), so a missing begin_geometry cannot silently
 * draw into the default target.
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
 *   tint_rgba      tint in the domain selected by tint_space (legacy entry
 *                  uses display). The shaded value stored in G0 is linear.
 *   normal_xyz     face world normal (same axis-aligned classes as flat).
 *   emissive_rgba  linear HDR self-emission (NOT textured in v1; albedo-tinted
 *                  at composite time, not independently additive).
 * COLOR/ALPHA CONTRACT: the legacy tex_face entry expects a DISPLAY texture
 * (plain GL_RGBA8, no per-texture GPU sRGB decode) and a DISPLAY tint. The
 * shader multiplies them in display space and performs one sRGB decode
 * before writing linear G0, matching the old SRGB8 G-buffer result. For an
 * explicitly SRGB/LINEAR source texture use zcsr_gl_l2d_geo_tex_face_ex:
 * an SRGB texture is decoded by the sampler, a LINEAR texture is used as-is,
 * and a DISPLAY tint is decoded explicitly while a LINEAR tint is used as-is.
 * Exactly one decode exists per channel. The composite output is encoded
 * through zcsr_color_pipeline's LUT, not a hard-coded pow formula.
 * ALPHA-CUTOUT v1 (explicit rule, not general
 * blending): a pixel whose sampled-alpha x tint-alpha is < 0.5 is DISCARDED
 * — it writes nothing, so the G-buffer surface submitted BENEATH it (earlier
 * in painter order) stays intact. No partial transparency, no sorting.
 * Texture alpha never becomes shadow geometry (occluders stay a separate
 * contract). */
typedef enum {
    /* tint_rgba is display-encoded; the shader decodes it before multiplying
     * linear texel color, exactly once per channel. */
    ZCSR_L2D_TINT_DISPLAY = 0,
    /* tint_rgba is linear-light; the multiply is component-wise in linear
     * space with no additional decode. */
    ZCSR_L2D_TINT_LINEAR = 1
} zcsr_l2d_tint_space;

bool zcsr_gl_l2d_geo_tex_face(zcsr_gl_renderer* renderer,
                              zcsr_gl_texture texture,
                              const float corners_xyz[12],
                              const float uv_st[8],
                              const float tint_rgba[4],
                              const float normal_xyz[3],
                              const float emissive_rgba[4]);

/* Extended textured face: explicit tint domain and an optional per-object
 * 3x3 color matrix (row-major, applied in linear light after tint). A NULL
 * matrix means identity. The albedo texture's color space comes from its
 * zcsr_texture_color_space metadata: SRGB is decoded by the sampler, LINEAR
 * is used as-is, and DISPLAY is decoded explicitly by the geometry shader.
 * Use ZCSR_L2D_TINT_LINEAR for a linear tint or ZCSR_L2D_TINT_DISPLAY for a
 * display-space tint. The legacy entry above is exactly
 * ZCSR_L2D_TINT_DISPLAY + identity and remains source-compatible. */
bool zcsr_gl_l2d_geo_tex_face_ex(zcsr_gl_renderer* renderer,
                                 zcsr_gl_texture texture,
                                 const float corners_xyz[12],
                                 const float uv_st[8],
                                 const float tint_rgba[4],
                                 zcsr_l2d_tint_space tint_space,
                                 const float color_matrix_row_major[9],
                                 const float normal_xyz[3],
                                 const float emissive_rgba[4]);

/* NORMAL-MAPPED textured face: same G-buffer path but two textures — an
 * albedo texture and a tangent-space normal map. The caller supplies both
 * the face normal and the tangent (unit world vectors); the engine builds
 * the TBN basis and writes the perturbed world normal to attachment 2.
 *   normal_tex    PUBLIC texture handle holding the tangent-space normal
 *                 map (RGB, stored as plain GL_RGBA; sampled, not color
 *                 managed). Normal maps are data, not sRGB color.
 *   tangent_xyz   unit world tangent aligned with the normal map's +U.
 * Other arguments match geo_tex_face, except the legacy tint parameter is
 * always display-domain (there is no nmap _ex entry yet). The albedo
 * texture's color-space metadata is honored; the normal map is data and is
 * never color managed. The same alpha-cutout rule applies.
 * Existing geo_face / geo_tex_face behavior is unchanged. */
bool zcsr_gl_l2d_geo_nmap_face(zcsr_gl_renderer* renderer,
                               zcsr_gl_texture albedo_tex,
                               zcsr_gl_texture normal_tex,
                               const float corners_xyz[12],
                               const float uv_st[8],
                               const float tint_rgba[4],
                               const float normal_xyz[3],
                               const float tangent_xyz[3],
                               const float emissive_rgba[4]);

bool zcsr_gl_l2d_geo_end(zcsr_gl_renderer* renderer);

#ifdef __cplusplus
}
#endif
#endif /* ZCSR_GL_RENDER_L2D_GEO_H */
