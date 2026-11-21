/* tools/gpu_probe.c — runtime GPU/backend diagnostic probe.
 *
 * Creates one real window, one GL 3.3 Core renderer, then prints the identity
 * that the default runtime path classified. No vendor switch, no macro, and no
 * special application call is needed to make Intel/AMD iGPUs work; this tool
 * only observes what the context already reports.
 *
 * Build (inside a configured build dir): link zcsr_glrender + zcsr_platform and
 * the normal platform libs. It is not part of the engine or test suite.
 */
#include "zcsr/platform.h"
#include "zcsr/gl_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

static const char* vendor_name(zcsr_gpu_vendor_class v) {
    switch (v) {
    case ZCSR_GPU_VENDOR_INTEL: return "intel";
    case ZCSR_GPU_VENDOR_AMD: return "amd";
    case ZCSR_GPU_VENDOR_NVIDIA: return "nvidia";
    case ZCSR_GPU_VENDOR_APPLE: return "apple";
    case ZCSR_GPU_VENDOR_SOFTWARE: return "software";
    default: return "unknown";
    }
}

static const char* driver_name(zcsr_gpu_driver_class d) {
    switch (d) {
    case ZCSR_GPU_DRIVER_IRIS: return "iris";
    case ZCSR_GPU_DRIVER_I965: return "i965";
    case ZCSR_GPU_DRIVER_RADEONSI: return "radeonsi";
    case ZCSR_GPU_DRIVER_ZINK: return "zink";
    case ZCSR_GPU_DRIVER_LLVMPIPE: return "llvmpipe";
    case ZCSR_GPU_DRIVER_NOUVEAU: return "nouveau";
    case ZCSR_GPU_DRIVER_NVIDIA: return "nvidia";
    case ZCSR_GPU_DRIVER_APPLE: return "apple";
    case ZCSR_GPU_DRIVER_MESA: return "mesa";
    default: return "generic";
    }
}

int main(void) {
    zcsr_rect bounds = { 80, 80, 320, 240 };
    zcsr_surface* surface;
    zcsr_gl_renderer* renderer;
    zcsr_gl_gpu_info info;
    void* arena;
    const size_t arena_bytes = 8u * 1024u * 1024u;
    int rc = 1;

    surface = zcsr_surface_create("ZCSR GPU probe", bounds);
    if (!surface) {
        fprintf(stderr, "gpu_probe: surface_create failed\n");
        return 2;
    }
    arena = malloc(arena_bytes);
    if (!arena) {
        zcsr_surface_destroy(surface);
        return 2;
    }
    renderer = zcsr_gl_create(surface, arena, arena_bytes);
    if (!renderer) {
        fprintf(stderr, "gpu_probe: GL 3.3 Core context creation failed\n");
        free(arena);
        zcsr_surface_destroy(surface);
        return 2;
    }
    memset(&info, 0, sizeof info);
    if (zcsr_gl_query_gpu_info(renderer, &info)) {
        printf("vendor=%s driver=%s integrated=%s known=%s software=%s backend=%s\n",
               vendor_name(info.vendor), driver_name(info.driver),
               info.integrated ? "yes" : "no",
               info.integrated_known ? "yes" : "no",
               info.software ? "yes" : "no", info.backend);
        printf("GL_VENDOR=\"%s\"\nGL_RENDERER=\"%s\"\nGL_VERSION=\"%s\"\n",
               info.vendor_string, info.renderer_string, info.version_string);
        rc = 0;
    } else {
        fprintf(stderr, "gpu_probe: query_gpu_info failed\n");
    }

    if (rc == 0) {
        /* A few real windowed frames plus one offscreen target. This catches
         * a context that was created but cannot actually render on this Mesa
         * driver; it is not a performance benchmark. */
        int frame;
        unsigned char clear_pixels[4] = { 32, 96, 160, 255 };
        unsigned char readback[4] = { 0, 0, 0, 0 };
        zcsr_gl_texture target = zcsr_gl_texture_create(renderer, clear_pixels, 1, 1);
        for (frame = 0; frame < 4; ++frame) {
            zcsr_gl_begin(renderer);
            zcsr_gl_clear(renderer, 0.08f, 0.10f, 0.14f, 1.0f);
            if (frame == 0) {
                glFinish();
                glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, readback);
            }
            zcsr_gl_present(renderer);
            zcsr_surface_pump_events(surface);
        }
        if (target) {
            zcsr_gl_set_target(renderer, target);
            zcsr_gl_begin(renderer);
            zcsr_gl_clear(renderer, 0.5f, 0.2f, 0.1f, 1.0f);
            zcsr_gl_flush(renderer);
            zcsr_gl_set_target(renderer, 0);
            zcsr_gl_texture_destroy(renderer, target);
        }
        printf("render=windowed4%s\n", target ? "+offscreen1" : "");
        printf("readback=%u,%u,%u,%u (expected ~20,26,36,255)\n",
               readback[0], readback[1], readback[2], readback[3]);
        zcsr_gl_present(renderer);
    }
    zcsr_gl_destroy(renderer);
    free(arena);
    zcsr_surface_destroy(surface);
    return rc;
}
