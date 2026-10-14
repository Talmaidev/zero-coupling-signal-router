/* Engine-ext Agent 1 — GPU rendering (OpenGL 3.3 Core). Additive optional renderer.
 *
 * This file holds the platform-independent GL logic plus the Linux/GLX and Windows/WGL backends.
 * The macOS/NSOpenGL backend is glrender_cocoa.m (compiled only when APPLE); the two share the
 * renderer layout and the platform hooks declared in glrender_internal.h.
 *
 * Includes ONLY shared contracts, the vendored glad loader, and native platform headers
 * (zero-coupling). No runtime heap: renderer state lives in the caller-provided buffer; GPU
 * objects and the native context are owned by the driver/OS.
 */
#include "glrender_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h> /* memset: zero the ~2.3 MB renderer in place, never via a stack compound literal */

#if defined(__ANDROID__)
#include <android/log.h>
#define ZCSR_GL_GLSL_VERSION "#version 300 es\n"
#define ZCSR_GL_GLSL_FRAGMENT_PRECISION "precision highp float;\n"
#else
#define ZCSR_GL_GLSL_VERSION "#version 330 core\n"
#define ZCSR_GL_GLSL_FRAGMENT_PRECISION ""
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#include <GL/glxext.h> /* GLX_CONTEXT_*_ARB + 3.3 core profile attributes */
#endif

/* ===========================================================================================
 * Platform-independent GL helpers (glad must already be loaded by zcsr_gl_platform_init)
 * =========================================================================================== */

static unsigned int zcsr_gl_compile(unsigned int type, const char* src) {
    unsigned int shader = glCreateShader(type);
    int ok = 0;
    if (!shader) return 0;
    glShaderSource(shader, 1, &src, 0);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static unsigned int zcsr_gl_make_program(void) {
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_pos;\n"
        "layout(location=1) in vec2 a_uv;\n"
        "layout(location=2) in vec4 a_color;\n"
        "layout(location=3) in vec2 a_mod;\n"
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_color;\n"
        "out vec2 v_mod;\n"
        "void main(){\n"
        "  vec2 p = vec2((a_pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (a_pos.y / u_view.y) * 2.0);\n"
        "  gl_Position = vec4(p, 0.0, 1.0);\n"
        "  v_uv = a_uv; v_color = a_color; v_mod = a_mod;\n"
        "}\n";
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "in vec2 v_uv;\n"
        "in vec4 v_color;\n"
        "in vec2 v_mod;\n"
        "uniform sampler2D u_tex;\n"
        "out vec4 o_color;\n"
        "void main(){\n"
        "  vec4 texel = texture(u_tex, v_uv);\n"
        "  float amount = clamp(v_mod.y, 0.0, 1.0);\n"
        "  if (v_mod.x < 0.5) o_color = texel * v_color;\n"
        "  else if (v_mod.x < 1.5) o_color = mix(texel, v_color, amount);\n"
        "  else o_color = clamp(texel + v_color * amount, 0.0, 1.0);\n"
        "}\n";
    unsigned int v = zcsr_gl_compile(GL_VERTEX_SHADER, vs);
    unsigned int f = zcsr_gl_compile(GL_FRAGMENT_SHADER, fs);
    unsigned int p;
    int ok = 0;
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    p = glCreateProgram();
    if (!p) {
        glDeleteShader(v);
        glDeleteShader(f);
        return 0;
    }
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static unsigned int zcsr_gl_make_inst_program(void) {
    /* Unit-quad corner (per-vertex) + per-instance attributes (divisor 1). The vertex shader
     * expands the corner into the rotated quad, so the CPU only uploads instance data. */
    static const char* vs =
        ZCSR_GL_GLSL_VERSION
        "layout(location=0) in vec2 a_corner;\n"
        "layout(location=1) in vec2 i_center;\n"
        "layout(location=2) in vec2 i_size;\n"
        "layout(location=3) in vec4 i_uv;\n"
        "layout(location=4) in vec4 i_color;\n"
        "layout(location=5) in float i_rotation;\n"
        "layout(location=6) in float i_amount;\n"
        "layout(location=7) in float i_mode;\n"
        "uniform vec2 u_view;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_color;\n"
        "out vec2 v_mod;\n"
        "void main(){\n"
        "  vec2 halfsize = i_size * 0.5;\n"
        "  float c = cos(i_rotation);\n"
        "  float s = sin(i_rotation);\n"
        "  vec2 local = a_corner * halfsize;\n"
        "  vec2 pos = i_center + vec2(local.x*c - local.y*s, local.x*s + local.y*c);\n"
        "  gl_Position = vec4((pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (pos.y / u_view.y) * 2.0, 0.0, 1.0);\n"
        "  vec2 t = a_corner * 0.5 + 0.5;\n"
        "  v_uv = mix(i_uv.xy, i_uv.zw, t);\n"
        "  v_color = i_color; v_mod = vec2(i_mode, i_amount);\n"
        "}\n";
    static const char* fs =
        ZCSR_GL_GLSL_VERSION
        ZCSR_GL_GLSL_FRAGMENT_PRECISION
        "in vec2 v_uv;\n"
        "in vec4 v_color;\n"
        "in vec2 v_mod;\n"
        "uniform sampler2D u_tex;\n"
        "out vec4 o_color;\n"
        "void main(){\n"
        "  vec4 texel = texture(u_tex, v_uv);\n"
        "  float amount = clamp(v_mod.y, 0.0, 1.0);\n"
        "  if (v_mod.x < 0.5) o_color = texel * v_color;\n"
        "  else if (v_mod.x < 1.5) o_color = mix(texel, v_color, amount);\n"
        "  else o_color = clamp(texel + v_color * amount, 0.0, 1.0);\n"
        "}\n";
    unsigned int v = zcsr_gl_compile(GL_VERTEX_SHADER, vs);
    unsigned int f = zcsr_gl_compile(GL_FRAGMENT_SHADER, fs);
    unsigned int p;
    int ok = 0;
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    p = glCreateProgram();
    if (!p) { glDeleteShader(v); glDeleteShader(f); return 0; }
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) { glDeleteProgram(p); return 0; }
    return p;
}

static unsigned int zcsr_gl_create_white_texture(void) {
    unsigned char px[4] = { 255, 255, 255, 255 };
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return tex;
}

static zcsr_gl_tex_entry* zcsr_gl_find_tex(zcsr_gl_renderer* r, zcsr_gl_texture t) {
    size_t i;
    if (!r || t == 0) return 0;
    for (i = 0; i < ZCSR_GL_MAX_TEXTURES; ++i) {
        if (r->textures[i].in_use && r->textures[i].id == t) return &r->textures[i];
    }
    return 0;
}

static zcsr_gl_tex_entry* zcsr_gl_free_tex(zcsr_gl_renderer* r) {
    size_t i;
    if (!r) return 0;
    for (i = 0; i < ZCSR_GL_MAX_TEXTURES; ++i) {
        if (!r->textures[i].in_use) return &r->textures[i];
    }
    return 0;
}

static void zcsr_gl_push_vertex(zcsr_gl_renderer* r, float x, float y, float u, float v,
                                float cr, float cg, float cb, float ca,
                                zcsr_color_mode mode, float amount) {
    zcsr_gl_vertex* out;
    if (!r || r->vertex_count >= ZCSR_GL_MAX_VERTICES) return;
    out = &r->vertices[r->vertex_count++];
    *out = (zcsr_gl_vertex){ x, y, u, v, cr, cg, cb, ca, (float)mode, amount };
}

static void zcsr_gl_push_quad(zcsr_gl_renderer* r, zcsr_rectf dst, float rot,
                              float cr, float cg, float cb, float ca,
                              zcsr_color_mode mode, float amount) {
    float hw = dst.w * 0.5f;
    float hh = dst.h * 0.5f;
    float cx = dst.x + hw;
    float cy = dst.y + hh;
    float c = cosf(rot);
    float s = sinf(rot);
    float px[4] = { -hw, hw, hw, -hw };
    float py[4] = { -hh, -hh, hh, hh };
    float vx[4], vy[4];
    int i;
    for (i = 0; i < 4; ++i) {
        vx[i] = cx + px[i] * c - py[i] * s;
        vy[i] = cy + px[i] * s + py[i] * c;
    }
    zcsr_gl_push_vertex(r, vx[0], vy[0], 0.0f, 0.0f, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[1], vy[1], 1.0f, 0.0f, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[2], vy[2], 1.0f, 1.0f, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[0], vy[0], 0.0f, 0.0f, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[2], vy[2], 1.0f, 1.0f, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[3], vy[3], 0.0f, 1.0f, cr, cg, cb, ca, mode, amount);
}

/* Quad with an explicit UV sub-rectangle (atlas sprites). Same geometry as push_quad but the
 * four corners sample [u0,u1] x [v0,v1] instead of the full texture. */
static void zcsr_gl_push_quad_uv(zcsr_gl_renderer* r, zcsr_rectf dst, float rot,
                                 float u0, float v0, float u1, float v1,
                                 float cr, float cg, float cb, float ca,
                                 zcsr_color_mode mode, float amount) {
    float hw = dst.w * 0.5f;
    float hh = dst.h * 0.5f;
    float cx = dst.x + hw;
    float cy = dst.y + hh;
    float c = cosf(rot);
    float s = sinf(rot);
    float px[4] = { -hw, hw, hw, -hw };
    float py[4] = { -hh, -hh, hh, hh };
    float vx[4], vy[4];
    int i;
    for (i = 0; i < 4; ++i) {
        vx[i] = cx + px[i] * c - py[i] * s;
        vy[i] = cy + px[i] * s + py[i] * c;
    }
    zcsr_gl_push_vertex(r, vx[0], vy[0], u0, v0, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[1], vy[1], u1, v0, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[2], vy[2], u1, v1, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[0], vy[0], u0, v0, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[2], vy[2], u1, v1, cr, cg, cb, ca, mode, amount);
    zcsr_gl_push_vertex(r, vx[3], vy[3], u0, v1, cr, cg, cb, ca, mode, amount);
}

static void zcsr_gl_record_draw(zcsr_gl_renderer* r, unsigned int texture, size_t first, size_t count) {
    if (!r || r->draw_count >= ZCSR_GL_MAX_SPRITES || count == 0) return;
    r->draws[r->draw_count++] = (zcsr_gl_draw){ texture, first, count };
}

/* Instanced resource setup/teardown (defined below, after the shared GL init). */
static bool zcsr_gl_init_inst(zcsr_gl_renderer* r);
static void zcsr_gl_destroy_inst(zcsr_gl_renderer* r);

/* Allocate the shared GPU resources (program, VAO/VBO, FBO, white texture). Runs after a context
 * is current and the GL function table is loaded. */
static bool zcsr_gl_init_gl(zcsr_gl_renderer* r) {
    r->program = zcsr_gl_make_program();
    if (!r->program) return false;
    r->uniform_view = glGetUniformLocation(r->program, "u_view");
    r->uniform_tex = glGetUniformLocation(r->program, "u_tex");

    glGenVertexArrays(1, &r->vao);
    glGenBuffers(1, &r->vbo);
    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(r->vertices), 0, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(zcsr_gl_vertex), (void*)offsetof(zcsr_gl_vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(zcsr_gl_vertex), (void*)offsetof(zcsr_gl_vertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(zcsr_gl_vertex), (void*)offsetof(zcsr_gl_vertex, r));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(zcsr_gl_vertex), (void*)offsetof(zcsr_gl_vertex, mode));

    glGenFramebuffers(1, &r->fbo);
    r->white_tex = zcsr_gl_create_white_texture();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!zcsr_gl_init_inst(r)) return false;
    return r->vao != 0 && r->vbo != 0 && r->fbo != 0 && r->white_tex != 0;
}

static void zcsr_gl_destroy_gl(zcsr_gl_renderer* r) {
    size_t i;
    for (i = 0; i < ZCSR_GL_MAX_TEXTURES; ++i) {
        if (r->textures[i].in_use && r->textures[i].id) glDeleteTextures(1, &r->textures[i].id);
    }
    if (r->white_tex) glDeleteTextures(1, &r->white_tex);
    if (r->fbo) glDeleteFramebuffers(1, &r->fbo);
    if (r->vbo) glDeleteBuffers(1, &r->vbo);
    if (r->vao) glDeleteVertexArrays(1, &r->vao);
    if (r->program) glDeleteProgram(r->program);
    zcsr_gl_destroy_inst(r);
}
static bool zcsr_gl_init_inst(zcsr_gl_renderer* r) {
    static const float corners[8] = {
        -1.f, -1.f,   1.f, -1.f,   1.f, 1.f,   -1.f, 1.f
    };
    static const unsigned short indices[6] = { 0, 1, 2, 0, 2, 3 };
    const size_t stride = sizeof(zcsr_gl_instance);

    r->inst_program = zcsr_gl_make_inst_program();
    if (!r->inst_program) return false;
    r->inst_uniform_view = glGetUniformLocation(r->inst_program, "u_view");
    r->inst_uniform_tex  = glGetUniformLocation(r->inst_program, "u_tex");

    glGenVertexArrays(1, &r->inst_vao);
    glBindVertexArray(r->inst_vao);

    glGenBuffers(1, &r->inst_corner_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_corner_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glGenBuffers(1, &r->inst_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->inst_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof indices, indices, GL_STATIC_DRAW);

    glGenBuffers(1, &r->inst_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, ZCSR_GL_MAX_INSTANCES * stride, NULL, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, cx));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, w));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, u0));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, r));
    glVertexAttribDivisor(4, 1);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, rotation));
    glVertexAttribDivisor(5, 1);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, amount));
    glVertexAttribDivisor(6, 1);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(zcsr_gl_instance, mode));
    glVertexAttribDivisor(7, 1);

    glBindVertexArray(0);
    return r->inst_vao != 0 && r->inst_vbo != 0;
}

static void zcsr_gl_destroy_inst(zcsr_gl_renderer* r) {
    if (r->inst_vbo) glDeleteBuffers(1, &r->inst_vbo);
    if (r->inst_ebo) glDeleteBuffers(1, &r->inst_ebo);
    if (r->inst_corner_vbo) glDeleteBuffers(1, &r->inst_corner_vbo);
    if (r->inst_vao) glDeleteVertexArrays(1, &r->inst_vao);
    if (r->inst_program) glDeleteProgram(r->inst_program);
}

/* ===========================================================================================
 * Linux / GLX backend
 * =========================================================================================== */
#if defined(__linux__) && !defined(__ANDROID__)
typedef GLXContext (*zcsr_glx_create_context_attribs_fn)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
typedef void (*zcsr_glx_swap_interval_ext_fn)(Display*, GLXDrawable, int);
typedef int (*zcsr_glx_swap_interval_mesa_fn)(unsigned int);
typedef int (*zcsr_glx_swap_interval_sgi_fn)(int);

static GLADgenericproc zcsr_glx_load(const char* name) {
    return (GLADgenericproc)glXGetProcAddressARB((const GLubyte*)name);
}

static GLXFBConfig zcsr_gl_choose_config(Display* display, int screen, VisualID visual_id) {
    int attrs[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 0,
        GLX_STENCIL_SIZE, 0,
        GLX_DOUBLEBUFFER, True,
        None
    };
    int count = 0;
    int i;
    GLXFBConfig result = 0;
    GLXFBConfig* configs = glXChooseFBConfig(display, screen, attrs, &count);
    if (!configs) return 0;
    for (i = 0; i < count; ++i) {
        XVisualInfo* info = glXGetVisualFromFBConfig(display, configs[i]);
        if (info) {
            if (info->visualid == visual_id && !result) result = configs[i];
            XFree(info);
        }
    }
    XFree(configs);
    return result;
}

bool zcsr_gl_make_current(zcsr_gl_renderer* r) {
    return r && r->display && r->window && r->ctx && glXMakeCurrent(r->display, r->window, r->ctx);
}

void zcsr_gl_update_size(zcsr_gl_renderer* r) {
    XWindowAttributes a;
    if (!r || !r->display || !r->window) return;
    if (XGetWindowAttributes(r->display, r->window, &a)) {
        r->width = a.width > 0 ? a.width : 1;
        r->height = a.height > 0 ? a.height : 1;
    }
}

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface) {
    Display* display = (Display*)zcsr_surface_native_display(surface);
    Window window = (Window)(uintptr_t)zcsr_surface_native_handle(surface);
    XWindowAttributes attrs;
    GLXFBConfig config;
    zcsr_glx_create_context_attribs_fn create_context;
    int context_attrs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None
    };
    int screen;
    if (!display || !window || !XGetWindowAttributes(display, window, &attrs)) return false;
    screen = DefaultScreen(display);
    config = zcsr_gl_choose_config(display, screen, XVisualIDFromVisual(attrs.visual));
    if (!config) return false;
    create_context = (zcsr_glx_create_context_attribs_fn)
        glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
    if (!create_context) return false;

    r->display = display;
    r->window = window;
    r->ctx = create_context(display, config, 0, True, context_attrs);
    if (!r->ctx) return false;
    if (!zcsr_gl_make_current(r)) {
        zcsr_gl_platform_destroy(r);
        return false;
    }
    if (!gladLoadGLLoader(zcsr_glx_load)) {
        zcsr_gl_platform_destroy(r);
        return false;
    }
    zcsr_gl_update_size(r);
    return true;
}

void zcsr_gl_platform_destroy(zcsr_gl_renderer* r) {
    if (r->display && r->ctx) {
        glXMakeCurrent(r->display, None, 0);
        glXDestroyContext(r->display, r->ctx);
    }
    r->ctx = 0;
    r->display = 0;
    r->window = 0;
}

void zcsr_gl_swap(zcsr_gl_renderer* r) {
    if (r && r->display && r->window) glXSwapBuffers(r->display, r->window);
}

void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on) {
    zcsr_glx_swap_interval_ext_fn ext;
    zcsr_glx_swap_interval_mesa_fn mesa;
    zcsr_glx_swap_interval_sgi_fn sgi;
    if (!r || !zcsr_gl_make_current(r)) return;
    ext = (zcsr_glx_swap_interval_ext_fn)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT");
    if (ext) { ext(r->display, r->window, on ? 1 : 0); return; }
    mesa = (zcsr_glx_swap_interval_mesa_fn)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalMESA");
    if (mesa) { mesa(on ? 1u : 0u); return; }
    sgi = (zcsr_glx_swap_interval_sgi_fn)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalSGI");
    if (sgi && on) (void)sgi(1);
}

/* ===========================================================================================
 * Android / EGL backend
 * =========================================================================================== */
#elif defined(__ANDROID__)
static bool zcsr_gl_egl_fail(zcsr_gl_renderer* r, const char* stage) {
    EGLint error = eglGetError();
    __android_log_print(ANDROID_LOG_ERROR, "zcsr-gl",
        "%s failed: EGL error=0x%x", stage, (unsigned int)error);
    zcsr_gl_platform_destroy(r);
    return false;
}

bool zcsr_gl_make_current(zcsr_gl_renderer* r) {
    return r && r->egl_initialized
        && r->display != EGL_NO_DISPLAY
        && r->egl_surface != EGL_NO_SURFACE
        && r->ctx != EGL_NO_CONTEXT
        && eglMakeCurrent(r->display, r->egl_surface, r->egl_surface, r->ctx) == EGL_TRUE;
}

void zcsr_gl_update_size(zcsr_gl_renderer* r) {
    EGLint width = 0, height = 0;
    if (!r || !r->egl_initialized || r->egl_surface == EGL_NO_SURFACE) return;
    if (eglQuerySurface(r->display, r->egl_surface, EGL_WIDTH, &width) == EGL_TRUE
        && eglQuerySurface(r->display, r->egl_surface, EGL_HEIGHT, &height) == EGL_TRUE) {
        r->width = width > 0 ? (int)width : 1;
        r->height = height > 0 ? (int)height : 1;
    }
}

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface) {
    ANativeWindow* window;
    EGLConfig config;
    EGLint major = 0, minor = 0, count = 0, visual_id = 0;
    const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE
    };
    const EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    const GLubyte* version;
    if (!r) return false;
    r->display = EGL_NO_DISPLAY;
    r->egl_surface = EGL_NO_SURFACE;
    r->ctx = EGL_NO_CONTEXT;
    r->egl_initialized = false;
    if (!surface) return false;
    window = (ANativeWindow*)zcsr_surface_native_handle(surface);
    if (!window) return false;
    r->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (r->display == EGL_NO_DISPLAY) return zcsr_gl_egl_fail(r, "eglGetDisplay");
    if (eglInitialize(r->display, &major, &minor) != EGL_TRUE)
        return zcsr_gl_egl_fail(r, "eglInitialize");
    r->egl_initialized = true;
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE)
        return zcsr_gl_egl_fail(r, "eglBindAPI");
    if (eglChooseConfig(r->display, config_attrs, &config, 1, &count) != EGL_TRUE)
        return zcsr_gl_egl_fail(r, "eglChooseConfig");
    if (count < 1) {
        __android_log_print(ANDROID_LOG_ERROR, "zcsr-gl",
            "No EGLConfig supports a GLES 3 window surface");
        zcsr_gl_platform_destroy(r);
        return false;
    }
    if (eglGetConfigAttrib(r->display, config, EGL_NATIVE_VISUAL_ID, &visual_id) != EGL_TRUE)
        return zcsr_gl_egl_fail(r, "eglGetConfigAttrib");
    if (ANativeWindow_setBuffersGeometry(window, 0, 0, visual_id) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "zcsr-gl",
            "ANativeWindow_setBuffersGeometry failed");
        zcsr_gl_platform_destroy(r);
        return false;
    }
    r->egl_surface = eglCreateWindowSurface(r->display, config,
        (EGLNativeWindowType)window, NULL);
    if (r->egl_surface == EGL_NO_SURFACE)
        return zcsr_gl_egl_fail(r, "eglCreateWindowSurface");
    r->ctx = eglCreateContext(r->display, config, EGL_NO_CONTEXT, context_attrs);
    if (r->ctx == EGL_NO_CONTEXT)
        return zcsr_gl_egl_fail(r, "eglCreateContext");
    if (eglMakeCurrent(r->display, r->egl_surface, r->egl_surface, r->ctx) != EGL_TRUE)
        return zcsr_gl_egl_fail(r, "eglMakeCurrent");
    version = glGetString(GL_VERSION);
    if (!version || strncmp((const char*)version, "OpenGL ES 3.", 12) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "zcsr-gl",
            "Expected GLES 3.x, actual version: %s",
            version ? (const char*)version : "(null)");
        zcsr_gl_platform_destroy(r);
        return false;
    }
    zcsr_gl_update_size(r);
    __android_log_print(ANDROID_LOG_INFO, "zcsr-gl",
        "EGL %d.%d; GLES=%s; size=%dx%d",
        major, minor, (const char*)version, r->width, r->height);
    return true;
}

void zcsr_gl_platform_destroy(zcsr_gl_renderer* r) {
    if (!r) return;
    if (r->display != EGL_NO_DISPLAY && r->egl_initialized) {
        (void)eglMakeCurrent(r->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (r->egl_surface != EGL_NO_SURFACE) (void)eglDestroySurface(r->display, r->egl_surface);
        if (r->ctx != EGL_NO_CONTEXT) (void)eglDestroyContext(r->display, r->ctx);
        (void)eglTerminate(r->display);
    }
    r->display = EGL_NO_DISPLAY;
    r->egl_surface = EGL_NO_SURFACE;
    r->ctx = EGL_NO_CONTEXT;
    r->egl_initialized = false;
    r->width = 0;
    r->height = 0;
}

void zcsr_gl_swap(zcsr_gl_renderer* r) {
    if (!r || !r->egl_initialized || r->egl_surface == EGL_NO_SURFACE) return;
    if (eglSwapBuffers(r->display, r->egl_surface) != EGL_TRUE) {
        EGLint error = eglGetError();
        __android_log_print(ANDROID_LOG_ERROR, "zcsr-gl",
            "eglSwapBuffers failed: 0x%x", (unsigned int)error);
    }
}

void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on) {
    if (!r || !r->egl_initialized) return;
    (void)eglSwapInterval(r->display, on ? 1 : 0);
}

/* ===========================================================================================
 * Windows / WGL backend
 * =========================================================================================== */
#elif defined(_WIN32)
/* WGL extension entry points (the Windows SDK ships no wglext.h): typed locally. */
typedef HGLRC (WINAPI *zcsr_wgl_create_context_attribs_fn)(HDC, HGLRC, const int*);
typedef BOOL (WINAPI *zcsr_wgl_swap_interval_fn)(int);

#define ZCSR_WGL_CONTEXT_MAJOR_VERSION_ARB   0x2091
#define ZCSR_WGL_CONTEXT_MINOR_VERSION_ARB   0x2092
#define ZCSR_WGL_CONTEXT_PROFILE_MASK_ARB    0x9126
#define ZCSR_WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

static GLADgenericproc zcsr_wgl_load(const char* name) {
    return (GLADgenericproc)wglGetProcAddress(name);
}

bool zcsr_gl_make_current(zcsr_gl_renderer* r) {
    return r && r->hdc && r->ctx && wglMakeCurrent(r->hdc, r->ctx) == TRUE;
}

void zcsr_gl_update_size(zcsr_gl_renderer* r) {
    RECT rect;
    if (!r || !r->hwnd) return;
    if (GetClientRect(r->hwnd, &rect)) {
        r->width = (rect.right - rect.left) > 0 ? (rect.right - rect.left) : 1;
        r->height = (rect.bottom - rect.top) > 0 ? (rect.bottom - rect.top) : 1;
    }
}

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface) {
    HWND hwnd = (HWND)zcsr_surface_native_handle(surface);
    HDC hdc;
    PIXELFORMATDESCRIPTOR pfd;
    int pf;
    HGLRC tmp;
    HGLRC ctx;
    zcsr_wgl_create_context_attribs_fn create33;
    int attribs[] = {
        ZCSR_WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        ZCSR_WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        ZCSR_WGL_CONTEXT_PROFILE_MASK_ARB, ZCSR_WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    if (!hwnd) return false;
    hdc = GetDC(hwnd);
    if (!hdc) return false;

    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
        ReleaseDC(hwnd, hdc);
        return false;
    }

    /* A legacy GL context is required before wglCreateContextAttribsARB can be queried; it is
     * used only to fetch the extension, then discarded in favour of the 3.3 Core context. */
    tmp = wglCreateContext(hdc);
    if (!tmp) {
        ReleaseDC(hwnd, hdc);
        return false;
    }
    if (!wglMakeCurrent(hdc, tmp)) {
        wglDeleteContext(tmp);
        ReleaseDC(hwnd, hdc);
        return false;
    }
    create33 = (zcsr_wgl_create_context_attribs_fn)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!create33) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(tmp);
        ReleaseDC(hwnd, hdc);
        return false;
    }
    ctx = create33(hdc, NULL, attribs);
    if (!ctx) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(tmp);
        ReleaseDC(hwnd, hdc);
        return false;
    }
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(tmp);
    if (!wglMakeCurrent(hdc, ctx)) {
        wglDeleteContext(ctx);
        ReleaseDC(hwnd, hdc);
        return false;
    }

    if (!gladLoadGLLoader(zcsr_wgl_load)) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(ctx);
        ReleaseDC(hwnd, hdc);
        return false;
    }

    r->hwnd = hwnd;
    r->hdc = hdc;
    r->ctx = ctx;
    zcsr_gl_update_size(r);
    return true;
}

void zcsr_gl_platform_destroy(zcsr_gl_renderer* r) {
    if (r->ctx) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(r->ctx);
        r->ctx = NULL;
    }
    if (r->hdc && r->hwnd) {
        ReleaseDC(r->hwnd, r->hdc);
        r->hdc = NULL;
    }
}

void zcsr_gl_swap(zcsr_gl_renderer* r) {
    if (r && r->hdc) SwapBuffers(r->hdc);
}

void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on) {
    zcsr_wgl_swap_interval_fn set_interval;
    if (!r) return;
    set_interval = (zcsr_wgl_swap_interval_fn)wglGetProcAddress("wglSwapIntervalEXT");
    if (set_interval) set_interval(on ? 1 : 0);
}

/* macOS: every zcsr_gl_* platform hook is implemented in glrender_cocoa.m. Nothing else here. */
#endif

/* ===========================================================================================
 * Public API (shared across every platform)
 * =========================================================================================== */

zcsr_gl_renderer* zcsr_gl_create(zcsr_surface* surface, void* buffer, size_t bytes) {
    zcsr_gl_renderer* r;
    if (!surface || !buffer || bytes < sizeof(zcsr_gl_renderer)) return 0;
    r = (zcsr_gl_renderer*)buffer;
    memset(r, 0, sizeof(*r));
    r->surface = surface;
    if (!zcsr_gl_platform_init(r, surface)) {
        memset(r, 0, sizeof(*r));
        return 0;
    }
    if (!zcsr_gl_init_gl(r)) {
        zcsr_gl_destroy(r);
        return 0;
    }
    return r;
}

void zcsr_gl_destroy(zcsr_gl_renderer* r) {
    if (!r) return;
    zcsr_gl_make_current(r);
    zcsr_gl_destroy_gl(r);
    zcsr_gl_platform_destroy(r);
    memset(r, 0, sizeof(*r));
}

void zcsr_gl_set_vsync(zcsr_gl_renderer* r, bool on) {
    if (!r) return;
    zcsr_gl_platform_set_vsync(r, on);
}

void zcsr_gl_present(zcsr_gl_renderer* r) {
    if (!r) return;
    zcsr_gl_swap(r);
}

void zcsr_gl_clear(zcsr_gl_renderer* r, float red, float green,
                   float blue, float alpha) {
    if (!r || !zcsr_gl_make_current(r)) return;
    /* This entry-point specifically clears the window back buffer. */
    r->target = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    zcsr_gl_update_size(r);
    glViewport(0, 0, r->width, r->height);
    glClearColor(red, green, blue, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
}

zcsr_gl_texture zcsr_gl_texture_create(zcsr_gl_renderer* r, const uint8_t* rgba, int w, int h) {
    zcsr_gl_tex_entry* e;
    unsigned int tex = 0;
    if (!r || !rgba || w <= 0 || h <= 0 || !zcsr_gl_make_current(r)) return 0;
    e = zcsr_gl_free_tex(r);
    if (!e) return 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    *e = (zcsr_gl_tex_entry){ tex, w, h, true };
    return tex;
}

void zcsr_gl_texture_filter(zcsr_gl_renderer* r, zcsr_gl_texture t, bool linear) {
    if (!r || !zcsr_gl_find_tex(r, t) || !zcsr_gl_make_current(r)) return;
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
}

bool zcsr_gl_texture_upload(zcsr_gl_renderer* r, zcsr_gl_texture t, const uint8_t* rgba, int w, int h) {
    zcsr_gl_tex_entry* e = zcsr_gl_find_tex(r, t);
    if (!e || !rgba || w <= 0 || h <= 0 || !zcsr_gl_make_current(r)) return false;
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    e->w = w;
    e->h = h;
    return true;
}

void zcsr_gl_texture_destroy(zcsr_gl_renderer* r, zcsr_gl_texture t) {
    zcsr_gl_tex_entry* e = zcsr_gl_find_tex(r, t);
    if (!e || !zcsr_gl_make_current(r)) return;
    glDeleteTextures(1, &e->id);
    *e = (zcsr_gl_tex_entry){ 0 };
}

void zcsr_gl_begin(zcsr_gl_renderer* r) {
    if (r) {
        r->vertex_count = 0;
        r->draw_count = 0;
    }
}

void zcsr_gl_submit(zcsr_gl_renderer* r, const zcsr_gl_sprite* s) {
    size_t first;
    if (!r || !s || s->tex == 0 || !zcsr_gl_find_tex(r, s->tex)) return;
    if (r->vertex_count + 6u > ZCSR_GL_MAX_VERTICES) return;
    first = r->vertex_count;
    zcsr_gl_push_quad(r, s->dst, s->rotation, s->r, s->g, s->b, s->a, s->mode, s->amount);
    zcsr_gl_record_draw(r, s->tex, first, r->vertex_count - first);
}

void zcsr_gl_flush(zcsr_gl_renderer* r) {
    size_t i;
    if (!r || r->vertex_count == 0 || !zcsr_gl_make_current(r)) return;
    if (r->target == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        zcsr_gl_update_size(r);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->target, 0);
    }
    glViewport(0, 0, r->width, r->height);
    glUseProgram(r->program);
    glUniform2f(r->uniform_view, (float)r->width, (float)r->height);
    glUniform1i(r->uniform_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(r->vertex_count * sizeof(zcsr_gl_vertex)), r->vertices);
    /* Coalesce contiguous runs that share a texture into one draw call. The bench submits
     * thousands of same-texture sprites; issuing one draw per sprite is driver-call bound. */
    i = 0;
    while (i < r->draw_count) {
        size_t j = i;
        size_t count = r->draws[i].count;
        while (j + 1 < r->draw_count
               && r->draws[j + 1].texture == r->draws[i].texture
               && r->draws[j + 1].first == r->draws[j].first + r->draws[j].count) {
            count += r->draws[j + 1].count;
            ++j;
        }
        glBindTexture(GL_TEXTURE_2D, r->draws[i].texture);
        glDrawArrays(GL_TRIANGLES, (GLint)r->draws[i].first, (GLsizei)count);
        i = j + 1;
    }
    if (r->target == 0) zcsr_gl_swap(r);
    r->vertex_count = 0;
    r->draw_count = 0;
}

void zcsr_gl_submit_batch(zcsr_gl_renderer* r, const zcsr_gl_sprite* sprites, size_t count) {
    size_t i;
    if (!r || !sprites) return;
    zcsr_gl_begin(r);
    for (i = 0; i < count; ++i) zcsr_gl_submit(r, &sprites[i]);
    zcsr_gl_flush(r);
}

static void zcsr_gl_submit_atlas(zcsr_gl_renderer* r, const zcsr_gl_atlas_sprite* s) {
    size_t first;
    if (!r || !s || s->atlas == 0 || !zcsr_gl_find_tex(r, s->atlas)) return;
    if (r->vertex_count + 6u > ZCSR_GL_MAX_VERTICES) return;
    first = r->vertex_count;
    zcsr_gl_push_quad_uv(r, s->dst, s->rotation,
                         s->region.u0, s->region.v0, s->region.u1, s->region.v1,
                         s->r, s->g, s->b, s->a, s->mode, s->amount);
    zcsr_gl_record_draw(r, s->atlas, first, r->vertex_count - first);
}

void zcsr_gl_submit_atlas_batch(zcsr_gl_renderer* r, const zcsr_gl_atlas_sprite* sprites,
                                size_t count) {
    size_t i;
    if (!r || !sprites) return;
    zcsr_gl_begin(r);
    for (i = 0; i < count; ++i) zcsr_gl_submit_atlas(r, &sprites[i]);
    zcsr_gl_flush(r);
}

/* ---- instanced rendering ---- */
void zcsr_gl_draw_instances(zcsr_gl_renderer* r, zcsr_gl_texture texture,
                            const zcsr_gl_instance* instances, size_t count) {
    if (!r || !instances || count == 0 || texture == 0) return;
    if (!zcsr_gl_find_tex(r, texture)) return;
    if (count > ZCSR_GL_MAX_INSTANCES) count = ZCSR_GL_MAX_INSTANCES;
    if (!zcsr_gl_make_current(r)) return;

    if (r->target == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        zcsr_gl_update_size(r);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               r->target, 0);
    }
    glViewport(0, 0, r->width, r->height);

    glUseProgram(r->inst_program);
    glUniform2f(r->inst_uniform_view, (float)r->width, (float)r->height);
    glUniform1i(r->inst_uniform_tex, 0);

    glBindVertexArray(r->inst_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->inst_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(zcsr_gl_instance), instances);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, (GLsizei)count);
    glBindVertexArray(0);
}

/* LSD radix sort of instances by tex (uint32), 4 passes of 8 bits. Scratch must hold
 * count instances. Stable, no heap. */
typedef uint32_t (*zcsr_instance_key_fn)(const zcsr_gl_instance*);

static void zcsr_gl_radix(zcsr_gl_instance* items, size_t count,
                          zcsr_gl_instance* scratch, zcsr_instance_key_fn key_of) {
    int shift, b;
    for (shift = 0; shift < 32; shift += 8) {
        uint32_t hist[256];
        size_t i;
        for (b = 0; b < 256; ++b) hist[b] = 0;
        for (i = 0; i < count; ++i) ++hist[(key_of(&items[i]) >> shift) & 0xFFu];
        uint32_t total = 0;
        for (b = 0; b < 256; ++b) {
            uint32_t c = hist[b];
            hist[b] = total;
            total += c;
        }
        for (i = 0; i < count; ++i) {
            uint32_t key = key_of(&items[i]);
            scratch[hist[(key >> shift) & 0xFFu]++] = items[i];
        }
        memcpy(items, scratch, count * sizeof(zcsr_gl_instance));
    }
}

static uint32_t zcsr_key_tex(const zcsr_gl_instance* in) { return in->tex; }

/* Map a float to a monotonic uint32 key, then invert for descending (far z first). */
static uint32_t zcsr_key_z_desc(const zcsr_gl_instance* in) {
    uint32_t u;
    memcpy(&u, &in->z, 4);
    uint32_t k = (u & 0x80000000u) ? ~u : (u | 0x80000000u);
    return ~k;
}

static void zcsr_gl_radix_by_tex(zcsr_gl_instance* items, size_t count,
                                 zcsr_gl_instance* scratch) {
    zcsr_gl_radix(items, count, scratch, zcsr_key_tex);
}

size_t zcsr_sort_instances(zcsr_gl_instance* instances, size_t count, void* temp, size_t temp_bytes) {
    size_t runs = 0, i;
    if (!instances || !temp || count == 0) return 0;
    if (temp_bytes < count * sizeof(zcsr_gl_instance)) return 0;
    zcsr_gl_radix_by_tex(instances, count, temp);
    for (i = 0; i < count; ++i) {
        if (i == 0 || instances[i].tex != instances[i - 1].tex) ++runs;
    }
    return runs;
}

void zcsr_instance_run(const zcsr_gl_instance* sorted, size_t count, size_t run_index,
                       zcsr_gl_texture* out_texture, size_t* out_start, size_t* out_len) {
    size_t runs = 0, start = 0, i;
    if (!sorted || !out_texture || !out_start || !out_len) return;
    for (i = 0; i <= count; ++i) {
        if (i == count || (i > 0 && sorted[i].tex != sorted[i - 1].tex)) {
            if (runs == run_index) {
                *out_texture = sorted[start].tex;
                *out_start = start;
                *out_len = i - start;
                return;
            }
            ++runs;
            start = i;
        }
    }
    *out_texture = 0;
    *out_start = 0;
    *out_len = 0;
}

void zcsr_gl_submit_instance_batch(zcsr_gl_renderer* r, zcsr_gl_instance* instances,
                                   size_t count, void* temp, size_t temp_bytes) {
    size_t runs, i;
    if (!r || !instances || !temp || count == 0) return;
    if (count > ZCSR_GL_MAX_INSTANCES) return;
    if (temp_bytes < count * sizeof(zcsr_gl_instance)) return;
    runs = zcsr_sort_instances(instances, count, temp, temp_bytes);
    for (i = 0; i < runs; ++i) {
        zcsr_gl_texture tex;
        size_t start, len;
        zcsr_instance_run(instances, count, i, &tex, &start, &len);
        if (tex) zcsr_gl_draw_instances(r, tex, instances + start, len);
    }
    if (r->target == 0) zcsr_gl_swap(r);
}

/* ---- full state sort (opaque vs translucent) ---- */
static int zcsr_is_translucent(const zcsr_gl_instance* in) {
    return in->a < 0.9999f ||
           in->mode == (float)ZCSR_MOD_MIX || in->mode == (float)ZCSR_MOD_ADD;
}

size_t zcsr_sort_state(zcsr_gl_instance* instances, size_t count, void* temp, size_t temp_bytes,
                       size_t* out_opaque) {
    zcsr_gl_instance* scratch = temp;
    size_t i = 0, j = count;
    if (!instances || !temp || count == 0) {
        if (out_opaque) *out_opaque = 0;
        return 0;
    }
    if (temp_bytes < count * sizeof(zcsr_gl_instance)) {
        if (out_opaque) *out_opaque = 0;
        return 0;
    }
    /* partition: opaque to the front, translucent to the back */
    while (i < j) {
        if (zcsr_is_translucent(&instances[i])) {
            zcsr_gl_instance t;
            --j;
            t = instances[i]; instances[i] = instances[j]; instances[j] = t;
        } else {
            ++i;
        }
    }
    {
        size_t opaque = i;
        if (opaque > 0) zcsr_gl_radix(instances, opaque, scratch, zcsr_key_tex);
        if (count - opaque > 0)
            zcsr_gl_radix(instances + opaque, count - opaque, scratch, zcsr_key_z_desc);
        if (out_opaque) *out_opaque = opaque;
        return opaque;
    }
}

void zcsr_gl_submit_state(zcsr_gl_renderer* r, zcsr_gl_instance* instances, size_t count,
                          void* temp, size_t temp_bytes) {
    size_t opaque;
    size_t i;
    if (!r || !instances || !temp || count == 0) return;
    if (count > ZCSR_GL_MAX_INSTANCES) return;
    if (temp_bytes < count * sizeof(zcsr_gl_instance)) return;
    zcsr_sort_state(instances, count, temp, temp_bytes, &opaque);

    /* opaque: draw in texture runs (order irrelevant) */
    i = 0;
    while (i < opaque) {
        size_t start = i;
        uint32_t tex = instances[i].tex;
        while (i < opaque && instances[i].tex == tex) ++i;
        if (tex) zcsr_gl_draw_instances(r, tex, instances + start, i - start);
    }
    /* translucent: back-to-front, gather only adjacent same-texture (z order preserved) */
    i = opaque;
    while (i < count) {
        size_t start = i;
        uint32_t tex = instances[i].tex;
        while (i < count && instances[i].tex == tex) ++i;
        if (tex) zcsr_gl_draw_instances(r, tex, instances + start, i - start);
    }
    if (r->target == 0) zcsr_gl_swap(r);
}

void zcsr_gl_set_target(zcsr_gl_renderer* r, zcsr_gl_texture target_or_zero) {
    zcsr_gl_tex_entry* e;
    if (!r || !zcsr_gl_make_current(r)) return;
    if (target_or_zero == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        r->target = 0;
        zcsr_gl_update_size(r);
        return;
    }
    e = zcsr_gl_find_tex(r, target_or_zero);
    if (!e) return;
    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_or_zero, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        r->target = 0;
        zcsr_gl_update_size(r);
        return;
    }
    r->target = target_or_zero;
    r->width = e->w;
    r->height = e->h;
}

void zcsr_gl_draw_text(zcsr_gl_renderer* r, const char* utf8, float x, float y, float scale,
                       float cr, float cg, float cb, float ca) {
    float pen = x;
    const unsigned char* p;
    if (!r || !utf8 || scale <= 0.0f) return;
    for (p = (const unsigned char*)utf8; *p; ++p) {
        unsigned char ch = *p;
        if (ch == '\n') {
            pen = x;
            y += 8.0f * scale;
            continue;
        }
        if (ch < 32u) continue;
        /* Minimal block glyph: enough for scores/debug labels without external font data. */
        if (r->vertex_count + 6u <= ZCSR_GL_MAX_VERTICES) {
            size_t first = r->vertex_count;
            zcsr_gl_push_quad(r, (zcsr_rectf){ pen, y, 5.0f * scale, 7.0f * scale }, 0.0f,
                              cr, cg, cb, ca, ZCSR_MOD_MULTIPLY, 0.0f);
            zcsr_gl_record_draw(r, r->white_tex, first, r->vertex_count - first);
        }
        pen += 6.0f * scale;
    }
}
