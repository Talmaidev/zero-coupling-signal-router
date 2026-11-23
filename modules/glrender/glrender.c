/* glrender.c — public API entry points and lifecycle coordination only.
 *
 * Rendering implementation lives in:
 *   glrender_core.c      — shader/program/VAO-VBO-FBO/white-tex/texture CRUD/clear/set_target
 *   glrender_sprite.c    — vertex queue/quad push/draw recording/flush/batch
 *   glrender_instanced.c — instanced program/init/destroy/draw/radix sort/state sort
 *   glrender_light2d_ext.c — Light2D contract impl (MRT G-buffer, state stack, radial/deferred)
 *   glrender_text.c      — 8x8 bitmap font text (ASCII 32-126)
 *   glrender_platform_*.c — Linux GLX / Android EGL / Windows WGL backends
 *   glrender_cocoa.m     — macOS NSOpenGL backend (unchanged)
 *
 * This file owns zcsr_gl_create/destroy (orchestrate platform → core → instanced → light2d
 * init order) and the trivial present/vsync forwarders. Every other public API is implemented
 * directly in the corresponding sub-file above.
 */
#include "glrender_internal.h"
#include "zcsr/diagnostics.h"

#include <string.h> /* memset: zero the renderer buffer */

/* ===========================================================================================
 * Lifecycle: platform_init → core_init → instanced_init (reverse on failure / destroy)
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
    /* Context is current now: classify GL_VENDOR/GL_RENDERER at runtime. This
     * never selects a vendor path and never fails creation; it makes the actual
     * Mesa driver visible on Intel/AMD iGPU machines. */
    zcsr_gl_detect_gpu(r);
    zcsr_gl_detect_capabilities(r);
    if (!zcsr_gl_core_init(r)) {
        /* core_init may have created partial GL resources (program/vao/vbo/fbo/white_tex/font_tex)
         * before failing — release them before tearing down platform. */
        zcsr_gl_core_destroy(r);
        zcsr_gl_platform_destroy(r);
        memset(r, 0, sizeof(*r));
        return 0;
    }
    if (!zcsr_gl_instanced_init(r)) {
        /* N3 fix: instanced_init may have created partial GL resources (program/VAO/VBOs)
         * before failing — release them before tearing down core/platform. */
        zcsr_gl_instanced_destroy(r);
        zcsr_gl_core_destroy(r);
        zcsr_gl_platform_destroy(r);
        memset(r, 0, sizeof(*r));
        return 0;
    }
    if (!zcsr_gl_l2d_init(r)) {
        /* Same rollback shape as the instanced_init failure above: l2d_init may have created
         * partial GL resources (programs/VAO/VBO/MRT slots) before failing. */
        zcsr_gl_l2d_destroy(r);
        zcsr_gl_instanced_destroy(r);
        zcsr_gl_core_destroy(r);
        zcsr_gl_platform_destroy(r);
        memset(r, 0, sizeof(*r));
        return 0;
    }
    /* The unified linear frame is the only Path A/B output. It requires an
     * RGBA16F color attachment; creation fails loudly if the device cannot
     * provide one instead of silently falling back to display-domain writes. */
    if (!zcsr_gl_frame_init(r)) {
        zcsr_gl_frame_destroy(r);
        zcsr_gl_l2d_destroy(r);
        zcsr_gl_instanced_destroy(r);
        zcsr_gl_core_destroy(r);
        zcsr_gl_platform_destroy(r);
        memset(r, 0, sizeof(*r));
        return 0;
    }
    return r;
}

bool zcsr_render_backend_available(zcsr_render_backend backend) {
    return backend == ZCSR_RENDER_BACKEND_GPU;
}

zcsr_gl_renderer* zcsr_gl_create_with_backend(zcsr_surface* surface, void* buffer,
                                               size_t bytes, zcsr_render_backend backend) {
    if (backend == ZCSR_RENDER_BACKEND_CPU) {
        ZCSR_LOG_ERROR("glrender", "CPU backend is reserved but not built; only the GPU runtime is available");
        return 0;
    }
    if (backend != ZCSR_RENDER_BACKEND_GPU) {
        ZCSR_LOG_ERROR("glrender", "unknown renderer backend %d", (int)backend);
        return 0;
    }
    return zcsr_gl_create(surface, buffer, bytes);
}

void zcsr_gl_destroy(zcsr_gl_renderer* r) {
    if (!r) return;
    zcsr_gl_make_current(r);
    zcsr_gl_frame_destroy(r);
    zcsr_gl_l2d_destroy(r);
    zcsr_gl_gpu_timer_destroy(r);
    zcsr_gl_instanced_destroy(r);
    zcsr_gl_core_destroy(r);
    zcsr_gl_platform_destroy(r);
    memset(r, 0, sizeof(*r));
}

/* ===========================================================================================
 * Trivial forwarders
 * =========================================================================================== */

void zcsr_gl_set_vsync(zcsr_gl_renderer* r, bool on) {
    if (!r) return;
    zcsr_gl_platform_set_vsync(r, on);
}

void zcsr_gl_present(zcsr_gl_renderer* r) {
    if (!r) return;
    if (!zcsr_gl_make_current(r)) return;
    if (r->target == 0) {
        if (!zcsr_gl_frame_encode(r)) {
            ZCSR_LOG_ERROR("glrender", "present: linear frame encode failed");
            return;
        }
    }
    zcsr_gl_swap(r);
}

bool zcsr_gl_read_rgba(zcsr_gl_renderer* r, void* rgba, size_t bytes,
                       int* out_w, int* out_h) {
    size_t need;
    if (!r || !rgba || r->width <= 0 || r->height <= 0) return false;
    need = (size_t)r->width * (size_t)r->height * 4u;
    if (bytes < need) {
        ZCSR_LOG_ERROR("glrender", "read_rgba: short buffer need=%zu bytes=%zu", need, bytes);
        return false;
    }
    if (!zcsr_gl_make_current(r)) {
        ZCSR_LOG_ERROR("glrender", "read_rgba: make_current failed");
        return false;
    }
    if (r->target == 0) {
        if (!zcsr_gl_frame_encode(r)) {
            ZCSR_LOG_ERROR("glrender",
                           "read_rgba: frame encode failed (frame_tex=%u prog=%u lut=%u %dx%d)",
                           r->frame_tex, r->frame_prog, r->l2d_color_lut_tex,
                           r->width, r->height);
            return false;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, r->fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, r->target, 0);
        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return false;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            ZCSR_LOG_ERROR("glrender", "read_rgba glReadPixels failed (GL error 0x%04x)",
                           (unsigned)err);
            return false;
        }
    }
    if (out_w) *out_w = r->width;
    if (out_h) *out_h = r->height;
    return true;
}
