/* glrender_caps.c — runtime GL capability gate.
 *
 * Probed once while the context is current. This is deliberately a real FBO
 * completeness check, not a driver-string guess: a GL 3.3 context can still
 * reject RGBA16F color attachments on constrained drivers, and Light2D's
 * deferred G-buffer is meaningless without float rendering. Path A remains
 * available regardless; callers that request Light2D must be able to see why
 * creation failed instead of getting a half-initialized renderer.
 */
#include "glrender_internal.h"
#include "zcsr/diagnostics.h"

#include <string.h>

/* The vendored loader intentionally carries only the constants the renderer
 * already used. These are core GL 3.0/3.3 enums; numeric fallbacks keep the
 * capability probe independent of optional SDK headers. */
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_MAX_COLOR_ATTACHMENTS
#define GL_MAX_COLOR_ATTACHMENTS 0x8CDF
#endif
#ifndef GL_MAX_DRAW_BUFFERS
#define GL_MAX_DRAW_BUFFERS 0x8824
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_MAX_TEXTURE_SIZE
#define GL_MAX_TEXTURE_SIZE 0x0D33
#endif
#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_EXTENSIONS
#define GL_EXTENSIONS 0x1F03
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif

typedef const char* (*zcsr_pfn_get_stringi)(unsigned int, unsigned int);

/* Resolve a GL entry point through the platform loader. Mirrors the proc
 * resolvers already used by the platform backends at context creation.
 * Non-static: shared by capability probing and by the G2 persistent-mapped
 * ring buffer (裁决 9). Returns NULL when name is NULL or the platform has
 * no loader for that entry. */
void* zcsr_gl_proc_address(const char* name) {
    if (!name) return NULL;
#if defined(_WIN32)
    return (void*)wglGetProcAddress(name);
#elif defined(__APPLE__)
    return NULL; /* core entries are exported directly by the framework */
#elif defined(__ANDROID__)
    return (void*)eglGetProcAddress(name);
#elif defined(__linux__) && defined(ZCSR_LINUX_BACKEND_X11)
    return (void*)glXGetProcAddressARB((const unsigned char*)name);
#elif defined(__linux__)
    return (void*)eglGetProcAddress(name);
#else
    return NULL;
#endif
}

/* F4 extra reporting probes. glrender_internal.h's renderer struct is owned
 * by another group and cannot gain slots here, so these values are cached in
 * this translation unit at detect time (the engine creates one renderer) and
 * copied into the public struct by zcsr_gl_query_capabilities. */
static bool  s_cap_gpu_timer = false;
static bool  s_cap_base_instance = false;
static bool  s_cap_indirect = false;
static int   s_cap_max_texture_size = 0;
static int   s_cap_max_msaa_samples = 0;
static float s_cap_max_anisotropy = 0.0f;
static bool  s_cap_linear_workflow = false;

static bool zcsr_gl_has_extension(const char* name) {
    static zcsr_pfn_get_stringi get_stringi = NULL;
    static int stringi_resolved = 0;
    GLint ext_count = 0;
    GLint i;
    if (!name) return false;

#if defined(__ANDROID__)
    /* GLES keeps the legacy GL_EXTENSIONS string query valid. */
    {
        const char* exts;
        const char* p;
        size_t name_len;
        exts = (const char*)glGetString(GL_EXTENSIONS);
        if (!exts) return false;
        name_len = strlen(name);
        p = exts;
        while (*p) {
            size_t n = 0;
            while (p[n] != '\0' && p[n] != ' ') ++n;
            if (n == name_len && strncmp(p, name, n) == 0) return true;
            p += n;
            while (*p == ' ') ++p;
        }
        return false;
    }
#else
    /* Desktop core >= 3.2: glGetString(GL_EXTENSIONS) is an invalid enum and
     * raises GL_INVALID_ENUM; extensions are enumerated with glGetStringi.
     * Resolve the entry through the platform loader once, current context. */
    if (!stringi_resolved) {
        stringi_resolved = 1;
#if defined(__APPLE__)
        get_stringi = (zcsr_pfn_get_stringi)glGetStringi;
#else
        get_stringi = (zcsr_pfn_get_stringi)zcsr_gl_proc_address("glGetStringi");
#endif
    }
    if (get_stringi) {
        glGetIntegerv(GL_NUM_EXTENSIONS, &ext_count);
        for (i = 0; i < ext_count; ++i) {
            const char* e = (const char*)get_stringi(GL_EXTENSIONS, (unsigned int)i);
            if (e && strcmp(e, name) == 0) return true;
        }
        return false;
    }
    /* Desktop core: the loader could not resolve glGetStringi. Return an
     * empty extension list rather than fall back to glGetString(GL_EXTENSIONS),
     * which is an invalid enum under a core profile and would raise
     * GL_INVALID_ENUM on WGL/GLX/EGL-Wayland/macOS CGL. */
    return false;
#endif
}

/* Ruling 9: ARB_buffer_storage advertisement for the persistent-mapped ring.
 * Real enumeration against the current context, not a driver-string guess. */
bool zcsr_gl_has_buffer_storage(void) {
    return zcsr_gl_has_extension("GL_ARB_buffer_storage");
}

static bool zcsr_gl_probe_color_format(GLenum internal_format, GLenum type) {
    GLuint tex = 0;
    GLuint fbo = 0;
    GLint previous_fbo = 0;
    GLenum status;
    bool ok = false;

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
    glGenTextures(1, &tex);
    if (tex == 0) return false;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal_format, 1, 1, 0,
                 GL_RGBA, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &fbo);
    if (fbo != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);
        status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        ok = (status == GL_FRAMEBUFFER_COMPLETE);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous_fbo);
        glDeleteFramebuffers(1, &fbo);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &tex);
    return ok;
}

void zcsr_gl_detect_capabilities(zcsr_gl_renderer* r) {
    GLint major = 0;
    GLint minor = 0;
    GLint max_attachments = 0;
    GLint max_draw_buffers = 0;
    const char* version;

    if (r == NULL) return;
    r->caps_probed = false;
    r->cap_core_33 = false;
    r->cap_gles3 = false;
    r->cap_float_color = false;
    r->cap_srgb8_color = false;
    r->cap_max_color_attachments = 0;
    r->cap_max_draw_buffers = 0;

    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    version = (const char*)glGetString(GL_VERSION);
    r->cap_core_33 = (major >= 4) || (major == 3 && minor >= 3);
    r->cap_gles3 = (major >= 3) &&
                   (version != NULL && strstr(version, "OpenGL ES") != NULL);
#if defined(__ANDROID__)
    r->cap_gles3 = (major >= 3);
#endif

    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_attachments);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &max_draw_buffers);
    r->cap_max_color_attachments = (int)max_attachments;
    r->cap_max_draw_buffers = (int)max_draw_buffers;
    r->cap_float_color = zcsr_gl_probe_color_format(GL_RGBA16F, GL_HALF_FLOAT);
    r->cap_srgb8_color = zcsr_gl_probe_color_format(GL_SRGB8_ALPHA8, GL_UNSIGNED_BYTE);

    /* ---- F4 pure reporting probes (no path selection) ---- */
    {
        GLint tex_size = 0;
        GLint msaa = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &tex_size);
        glGetIntegerv(GL_MAX_SAMPLES, &msaa);
        s_cap_max_texture_size = (int)tex_size;
        s_cap_max_msaa_samples = (int)msaa;
    }
#if defined(__ANDROID__)
    s_cap_gpu_timer = zcsr_gl_has_extension("GL_EXT_disjoint_timer_query");
    s_cap_base_instance = zcsr_gl_has_extension("GL_EXT_base_instance");
    s_cap_indirect = zcsr_gl_has_extension("GL_EXT_draw_indirect");
#else
    /* Desktop: GL_TIME_ELAPSED, base instance (4.2) and indirect draw (4.0)
     * are core above those versions; otherwise report the ARB extension. */
    s_cap_gpu_timer = r->cap_core_33;
    s_cap_base_instance = (major > 4 || (major == 4 && minor >= 2)) ||
                          zcsr_gl_has_extension("GL_ARB_base_instance");
    s_cap_indirect = (major >= 4) || zcsr_gl_has_extension("GL_ARB_draw_indirect");
#endif
    if (zcsr_gl_has_extension("GL_EXT_texture_filter_anisotropic")) {
        GLfloat aniso = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &aniso);
        s_cap_max_anisotropy = aniso;
    } else {
        s_cap_max_anisotropy = 0.0f;
    }
    /* Ruling 8 / C1: the linear RGBA16F frame + encode pass exists only in the
     * LINEAR_HDR color tier. DIRECT_SRGB writes the default framebuffer
     * directly, so reporting a linear workflow there would be a false capability.
     * Decided at compile time by the same ZCSR_COLOR_TIER macro as the shaders. */
#if ZCSR_COLOR_TIER == ZCSR_COLOR_OPT_DIRECT_SRGB
    s_cap_linear_workflow = false;
#else
    s_cap_linear_workflow = true;
#endif

    r->caps_probed = true;

    ZCSR_LOG_INFO("gl",
                  "caps: gl=%d.%d core33=%s gles3=%s float_color=%s "
                  "srgb8_color=%s mrt4=%s max_color_attachments=%d "
                  "max_draw_buffers=%d gpu_timer=%s base_instance=%s "
                  "indirect=%s tex_max=%d msaa_max=%d aniso_max=%.1f linear=%s",
                  (int)major, (int)minor,
                  r->cap_core_33 ? "yes" : "no",
                  r->cap_gles3 ? "yes" : "no",
                  r->cap_float_color ? "yes" : "no",
                  r->cap_srgb8_color ? "yes" : "no",
                  (max_attachments >= 4 && max_draw_buffers >= 4) ? "yes" : "no",
                  (int)max_attachments, (int)max_draw_buffers,
                  s_cap_gpu_timer ? "yes" : "no",
                  s_cap_base_instance ? "yes" : "no",
                  s_cap_indirect ? "yes" : "no",
                  s_cap_max_texture_size, s_cap_max_msaa_samples,
                  (double)s_cap_max_anisotropy,
                  s_cap_linear_workflow ? "yes" : "no");
}

bool zcsr_gl_query_capabilities(const zcsr_gl_renderer* r,
                                zcsr_gl_capabilities* out) {
    if (r == NULL || out == NULL || !r->caps_probed) return false;
    memset(out, 0, sizeof(*out));
    out->core_33 = r->cap_core_33;
    out->gles3 = r->cap_gles3;
    out->float_color_buffer = r->cap_float_color;
    out->srgb8_color_buffer = r->cap_srgb8_color;
    out->max_color_attachments = r->cap_max_color_attachments;
    out->max_draw_buffers = r->cap_max_draw_buffers;
    out->mrt4 = out->max_color_attachments >= 4 && out->max_draw_buffers >= 4;
    out->gpu_timer_available = s_cap_gpu_timer;
    out->instanced_base_instance = s_cap_base_instance;
    out->indirect_draw = s_cap_indirect;
    out->max_texture_size = s_cap_max_texture_size;
    out->max_msaa_samples = s_cap_max_msaa_samples;
    out->max_anisotropy = s_cap_max_anisotropy;
    out->linear_workflow = s_cap_linear_workflow;
    return true;
}
