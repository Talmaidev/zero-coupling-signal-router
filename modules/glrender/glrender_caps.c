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
    r->caps_probed = true;

    ZCSR_LOG_INFO("gl",
                  "caps: gl=%d.%d core33=%s gles3=%s float_color=%s "
                  "srgb8_color=%s mrt4=%s max_color_attachments=%d "
                  "max_draw_buffers=%d",
                  (int)major, (int)minor,
                  r->cap_core_33 ? "yes" : "no",
                  r->cap_gles3 ? "yes" : "no",
                  r->cap_float_color ? "yes" : "no",
                  r->cap_srgb8_color ? "yes" : "no",
                  (max_attachments >= 4 && max_draw_buffers >= 4) ? "yes" : "no",
                  (int)max_attachments, (int)max_draw_buffers);
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
    return true;
}
