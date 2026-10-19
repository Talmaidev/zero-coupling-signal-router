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
#  include "glad/glad.h"   /* pulls in GL/gl.h first ... */
#  include <GL/glx.h>      /* ... which glx.h builds on */
#  include <X11/Xlib.h>
#  include <X11/Xutil.h>
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

    /* ---- native platform context (exactly one branch compiles) ---- */
#if defined(__ANDROID__)
    EGLDisplay display;
    EGLSurface egl_surface;
    EGLContext ctx;
    bool egl_initialized;
#elif defined(__linux__) && !defined(__ANDROID__)
    Display*   display;
    Window     window;
    GLXContext ctx;
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

#endif /* ZCSR_GLRENDER_INTERNAL_H */
