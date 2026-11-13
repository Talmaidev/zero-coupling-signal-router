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
    return r;
}

void zcsr_gl_destroy(zcsr_gl_renderer* r) {
    if (!r) return;
    zcsr_gl_make_current(r);
    zcsr_gl_l2d_destroy(r);
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
    zcsr_gl_swap(r);
}
