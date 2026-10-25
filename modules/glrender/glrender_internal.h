#ifndef ZCSR_GLRENDER_INTERNAL_H
#define ZCSR_GLRENDER_INTERNAL_H
/* Module-private shared definitions for the GL renderer. This is NOT a public contract — it is
 * included only by the translation units of modules/glrender:
 *   - glrender.c        : platform-independent GL logic + Linux/GLX, Windows/WGL, Android/EGL backends
 *   - glrender_cocoa.m  : the macOS/NSOpenGL backend (compiled only when APPLE)
 * The zero-coupling guard (tools/check_no_cross_includes.sh) bans "../" and "modules/" includes;
 * a same-directory internal header like this one is allowed and never leaves the module.
 */

#include "zcsr/gl_render.h"

/* Platform system headers must precede the vendored GL loader where the platform's GL headers
 * depend on them (Windows: windows.h before GL/gl.h). */
#if defined(__ANDROID__)
#  include <android/native_window.h>
#  include <android/log.h>
#  include <EGL/egl.h>
#  include <EGL/eglext.h>
#  include <GLES3/gl3.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#  include "glad/glad.h"
#  include <EGL/egl.h>
#  include <EGL/eglext.h>
#  include <wayland-egl.h>
#elif defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include "glad/glad.h"   /* GL/gl.h + glext.h from the Windows SDK */
#elif defined(__APPLE__)
#  include "glad/glad.h"   /* the loader header switches to OpenGL/gl3.h on Apple */
#endif

/* GLSL version selection: GLES 3.0 on Android, GLSL 330 core everywhere else. */
#if defined(__ANDROID__)
#  define ZCSR_GL_GLSL_VERSION "#version 300 es\n"
#  define ZCSR_GL_GLSL_FRAGMENT_PRECISION "precision highp float;\n"
#else
#  define ZCSR_GL_GLSL_VERSION "#version 330 core\n"
#  define ZCSR_GL_GLSL_FRAGMENT_PRECISION ""
#endif

enum {
    ZCSR_GL_MAX_SPRITES = 10000,
    ZCSR_GL_MAX_VERTICES = ZCSR_GL_MAX_SPRITES * 6,
    ZCSR_GL_MAX_TEXTURES = 128,
    ZCSR_GL_MAX_INSTANCES = 10000
};

/* Interleaved vertex: pos(2) + uv(2) + color(4) + mode(1) + amount(1) = 10 floats. */
typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
    float mode, amount;
} zcsr_gl_vertex;

typedef struct {
    unsigned int id;
    int w, h;
    bool in_use;
} zcsr_gl_tex_entry;

/* One contiguous run of vertices sharing one texture = one glDrawArrays call. */
typedef struct {
    unsigned int texture;
    size_t first;
    size_t count;
} zcsr_gl_draw;

/* ---- Light2D extension (implementation in glrender_light2d_ext.c) -------------------------- */
enum {
    ZCSR_GL_L2D_MAX_TARGETS = 2,      /* MRT slot handles are slot index + 1; 0 = invalid */
    ZCSR_GL_L2D_MAX_ATTACHMENTS = 4,  /* contract order: Albedo, Position, Normal, Emissive */
    ZCSR_GL_L2D_STATE_DEPTH = 8,      /* save/restore stack depth */
    ZCSR_GL_L2D_STATE_TEX_UNITS = 5   /* texture units 4..8 touched by the extension */
};

/* One MRT G-buffer target: up to 4 color attachments on one FBO. iformat/attachments are
 * remembered so target_resize can rebuild the same configuration transactionally. */
typedef struct {
    unsigned int fbo;
    unsigned int tex[ZCSR_GL_L2D_MAX_ATTACHMENTS];
    int w, h;
    int iformat[ZCSR_GL_L2D_MAX_ATTACHMENTS];
    int attachments;
    bool in_use;
} zcsr_gl_l2d_mrt;

/* Snapshot of every GL state field the extension touches across a frame: bound draw FBO,
 * viewport, blend (enable + equation + func, RGB and Alpha), current program, active texture
 * unit and the 2D bindings on units 4..8. draw_quad/draw_fullscreen_quad additionally restore
 * the VAO and ARRAY_BUFFER bindings locally per call. */
typedef struct {
    int fbo;
    int viewport[4];
    bool blend;
    int blend_equation_rgb, blend_equation_alpha;
    int blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha;
    int active_texture;
    unsigned int textures[ZCSR_GL_L2D_STATE_TEX_UNITS];
    unsigned int program;
} zcsr_gl_l2d_state;

/* The opaque renderer, laid out here so both translation units see the same fields. Everything
 * lives in the caller-provided buffer (no malloc); the only platform-varying members are the
 * native context handles at the tail, selected by the same compile-time gates as the platform.h
 * contract. */
struct zcsr_gl_renderer {
    /* ---- platform-independent state ---- */
    zcsr_surface* surface;
    int width;
    int height;
    zcsr_gl_vertex vertices[ZCSR_GL_MAX_VERTICES];
    size_t vertex_count;
    zcsr_gl_draw draws[ZCSR_GL_MAX_SPRITES];
    size_t draw_count;
    zcsr_gl_tex_entry textures[ZCSR_GL_MAX_TEXTURES];
    /* O5: direct lookup table for texture handle -> textures[] slot.
     * 256 slots (2x pool size, <50% load), open addressing. key=0 means empty. */
    uint32_t tex_index_key[256];
    uint8_t  tex_index_val[256];
    unsigned int program;
    unsigned int vao;
    unsigned int vbo;
    unsigned int white_tex;
    unsigned int font_tex;
    unsigned int fbo;
    zcsr_gl_texture target;
    int uniform_view;
    int uniform_tex;

    /* ---- instanced rendering resources (unit quad + per-instance VBO) ---- */
    unsigned int inst_program;
    unsigned int inst_vao;
    unsigned int inst_corner_vbo;
    unsigned int inst_ebo;
    unsigned int inst_vbo;
    int inst_uniform_view;
    int inst_uniform_tex;

    /* ---- Light2D extension (driven by modules/light2d through the public
     * gl_render_light2d_ext.h contract; state is zeroed by zcsr_gl_create's memset) ---- */
    zcsr_gl_l2d_mrt l2d_targets[ZCSR_GL_L2D_MAX_TARGETS];
    int l2d_state_depth;
    zcsr_gl_l2d_state l2d_state_stack[ZCSR_GL_L2D_STATE_DEPTH];
    unsigned int l2d_prog_radial;
    unsigned int l2d_prog_deferred;
    unsigned int l2d_prog_bloom;
    unsigned int fsq_vao;
    unsigned int fsq_vbo;
    int l2d_radial_loc_view;
    int l2d_radial_loc_radius;
    int l2d_radial_loc_decay;
    int l2d_radial_loc_color;
    int l2d_bloom_loc_ambient;
    int l2d_bloom_loc_albedo;
    int l2d_bloom_loc_emissive;
    int l2d_bloom_loc_light;
    int l2d_bloom_loc_lightres;
    int l2d_deferred_loc_ambient;
    int l2d_deferred_loc_albedo;
    int l2d_deferred_loc_position;
    int l2d_deferred_loc_normal;
    int l2d_deferred_loc_emissive;
    int l2d_deferred_loc_shadow;
    int l2d_deferred_loc_viewsize;
    int l2d_deferred_loc_lightcount;
    int l2d_deferred_loc_lightdata;
    int l2d_deferred_loc_lightcolor;
    /* Light2d v2 (JoyECS-style): shadow-mask pass + light accumulation buffer. */
    unsigned int l2d_prog_shadow;
    unsigned int l2d_prog_light;
    unsigned int l2d_shadow_fbo;
    unsigned int l2d_shadow_tex;
    int l2d_shadow_w, l2d_shadow_h;
    unsigned int l2d_light_fbo;
    unsigned int l2d_light_tex;
    int l2d_light_w, l2d_light_h;
    int l2d_shadow_loc_light;
    int l2d_shadow_loc_view;
    int l2d_shadow_loc_factor;
    int l2d_shadow_loc_dir;      /* u_dir: parallel extrusion direction */
    int l2d_shadow_loc_distance; /* u_distance: extrusion length (px) */
    int l2d_shadow_loc_parallel; /* u_parallel: 1 = parallel light mode */
    int l2d_shadow_loc_reverse;  /* u_reverse: extrude toward the light */
    int l2d_light_loc_light;
    int l2d_light_loc_view;
    int l2d_light_loc_viewpos;
    int l2d_light_loc_color;
    int l2d_light_loc_radius;
    int l2d_light_loc_decay;
    int l2d_light_loc_gain;
    int l2d_light_loc_normal;
    int l2d_light_loc_shadow;
    int l2d_light_loc_use_shadow;
    int l2d_light_loc_normals_on;
    int l2d_light_loc_dist_scale;
    int l2d_light_loc_light_z;   /* u_light_z: flame height above the plane */
    int l2d_light_loc_shadowres; /* u_shadowres: mask size for the 5-tap bias */
    int l2d_light_loc_position;  /* u_Position sampler (unit 5, world 3D) */
    int l2d_light_loc_px_per_unit; /* camera zoom: px per world unit */
    int l2d_light_loc_parallel;  /* u_parallel: 1 = parallel light */
    int l2d_light_loc_dir3;      /* u_dir3: parallel unit travel direction */
    float l2d_px_per_unit;       /* active zoom for world->px quad sizing */
    int l2d_view_w, l2d_view_h;  /* view size for parallel fullscreen quads */

    /* ---- G-buffer geometry submission (gl_render_l2d_geo.h; lazy-init on
     * first geo_begin, released from zcsr_gl_core_destroy) ---- */
    unsigned int l2d_geo_prog;
    unsigned int l2d_geo_vao;
    unsigned int l2d_geo_vbo;
    int l2d_geo_loc_cam, l2d_geo_loc_ppu, l2d_geo_loc_k, l2d_geo_loc_view;
    int l2d_geo_loc_albedo, l2d_geo_loc_emissive;
    /* textured variant: own program+VAO/VBO, samples the host texture on unit 0 */
    unsigned int l2d_geo_tex_prog;
    unsigned int l2d_geo_tex_vao;
    unsigned int l2d_geo_tex_vbo;
    int l2d_geo_tex_loc_cam, l2d_geo_tex_loc_ppu, l2d_geo_tex_loc_k, l2d_geo_tex_loc_view;
    int l2d_geo_tex_loc_tint, l2d_geo_tex_loc_emissive, l2d_geo_tex_loc_sampler;
    bool l2d_geo_active;
    GLint l2d_geo_saved_prog, l2d_geo_saved_vao, l2d_geo_saved_vbo;
    GLint l2d_geo_saved_blend, l2d_geo_saved_depth, l2d_geo_saved_cull;
    GLint l2d_geo_saved_active_tex, l2d_geo_saved_unit0_tex;
    int l2d_deferred_loc_light; /* u_Light sampler in the composite fs */

    /* ---- native platform context (exactly one branch compiles) ---- */
#if defined(__ANDROID__)
    EGLDisplay display;
    EGLSurface egl_surface;
    EGLContext ctx;
    bool egl_initialized;
#elif defined(__linux__) && !defined(__ANDROID__)
    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;
    struct wl_egl_window* egl_window;
#elif defined(_WIN32)
    HWND  hwnd;
    HDC   hdc;
    HGLRC ctx;
#elif defined(__APPLE__)
    /* NSOpenGLContext*, retained across the C/ObjC boundary with bridge casts in glrender_cocoa.m
     * (__bridge_retained on store, __bridge_transfer on release). Stored as void* so this header
     * and the pure-C translation units never need ObjC syntax. */
    void* ctx;
#endif
};

/* ---- Platform backend -----------------------------------------------------------------------
 * One definition of each exists per target: in glrender.c for Linux/Windows/Android, in glrender_cocoa.m
 * for macOS. zcsr_gl_create/destroy (shared, in glrender.c) call these around the GL setup.
 *
 * platform_init: create a GL 3.3 Core context on the surface, make it current, and load the GL
 *                function table (glad). Return false on any failure (no usable GL on this surface).
 * platform_destroy: tear down the context and release the platform connection (DC / X display).
 */
bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface);
void zcsr_gl_platform_destroy(zcsr_gl_renderer* r);
bool zcsr_gl_make_current(zcsr_gl_renderer* r);
void zcsr_gl_update_size(zcsr_gl_renderer* r);
void zcsr_gl_swap(zcsr_gl_renderer* r);
void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on);

/* ---- Core (shader/program/VAO/VBO/FBO/white-tex/texture CRUD/clear/set_target) -------------- */
/* Shared shader helpers (used by both core sprite-program and instanced program). */
void zcsr_gl_log_object_info(const char* label, unsigned int object, bool is_program);
unsigned int zcsr_gl_compile(unsigned int type, const char* src);

bool zcsr_gl_core_init(zcsr_gl_renderer* r);
void zcsr_gl_core_destroy(zcsr_gl_renderer* r);
/* glrender_l2d_geo.c: releases the lazily-created geometry program/VAO/VBO
 * (GL context must be current — core destroy runs inside one). */
void zcsr_gl_l2d_geo_cleanup(zcsr_gl_renderer* r);
zcsr_gl_tex_entry* zcsr_gl_find_tex(zcsr_gl_renderer* r, zcsr_gl_texture t);
zcsr_gl_tex_entry* zcsr_gl_free_tex(zcsr_gl_renderer* r);

/* ---- Sprite (vertex queue / quad push / draw recording / flush / batch) ------------------- */
void zcsr_gl_push_vertex(zcsr_gl_renderer* r, float x, float y, float u, float v,
                         float cr, float cg, float cb, float ca,
                         zcsr_color_mode mode, float amount);
void zcsr_gl_push_quad(zcsr_gl_renderer* r, zcsr_rectf dst, float rot,
                       float cr, float cg, float cb, float ca,
                       zcsr_color_mode mode, float amount);
void zcsr_gl_push_quad_uv(zcsr_gl_renderer* r, zcsr_rectf dst, float rot,
                          float u0, float v0, float u1, float v1,
                          float cr, float cg, float cb, float ca,
                          zcsr_color_mode mode, float amount);
void zcsr_gl_record_draw(zcsr_gl_renderer* r, unsigned int texture, size_t first, size_t count);

/* ---- Instanced (program/init/destroy/draw/radix sort/state sort) --------------------------- */
bool zcsr_gl_instanced_init(zcsr_gl_renderer* r);
void zcsr_gl_instanced_destroy(zcsr_gl_renderer* r);

/* ---- Light2D extension (MRT G-buffer targets, state stack, radial/deferred programs) ------- */
/* Called from zcsr_gl_create/destroy only; every public entry point of the extension contract
 * (include/zcsr/gl_render_light2d_ext.h) is implemented in glrender_light2d_ext.c. */
bool zcsr_gl_l2d_init(zcsr_gl_renderer* r);
void zcsr_gl_l2d_destroy(zcsr_gl_renderer* r);

#endif /* ZCSR_GLRENDER_INTERNAL_H */
