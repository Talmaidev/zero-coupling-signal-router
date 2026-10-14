/* zcsr Android Phase 1B — EGL/GLES lifecycle + clear-frame proof. */
#include <android_native_app_glue.h>
#include <android/log.h>
#include <GLES3/gl3.h>

#include <stddef.h>
#include <stdint.h>

#include "zcsr/platform.h"
#include "zcsr/clock.h"
#include "zcsr/gl_render.h"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "zcsr", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "zcsr", __VA_ARGS__))

#define ZCSR_GL_BUFFER_BYTES (8u * 1024u * 1024u)

/* Aligned, fixed-size storage. No runtime heap allocation. */
static union {
    max_align_t alignment;
    unsigned char bytes[ZCSR_GL_BUFFER_BYTES];
} zcsr_renderer_storage;

typedef struct {
    zcsr_surface* surface;
    zcsr_gl_renderer* renderer;
} zcsr_android_runtime;

static void zcsr_android_stop_gl(zcsr_android_runtime* runtime) {
    if (!runtime) return;

    if (runtime->renderer) {
        zcsr_gl_destroy(runtime->renderer);
        runtime->renderer = 0;
        LOGI("EGL renderer destroyed");
    }
}

static void zcsr_android_start_gl(
    zcsr_android_runtime* runtime, void* native_window) {
    const GLubyte* version;
    void* currently_bound;

    if (!runtime || !runtime->surface || !native_window) return;

    currently_bound = zcsr_surface_native_handle(runtime->surface);

    /* A duplicate INIT_WINDOW for the same live window is harmless. */
    if (runtime->renderer && currently_bound == native_window) return;

    /* If replacing a live window, destroy EGL before platform releases its reference. */
    zcsr_android_stop_gl(runtime);

    if (currently_bound != native_window) {
        if (!zcsr_surface_attach_native(runtime->surface, native_window)) {
            LOGE("surface attach failed");
            return;
        }
    }

    runtime->renderer = zcsr_gl_create(
        runtime->surface,
        zcsr_renderer_storage.bytes,
        sizeof(zcsr_renderer_storage.bytes));

    if (!runtime->renderer) {
        LOGE("zcsr_gl_create failed; inspect zcsr-gl EGL/shader logs");
        return;
    }

    version = glGetString(GL_VERSION);
    LOGI("GLES version: %s",
         version ? (const char*)version : "(null)");

    zcsr_gl_clear(runtime->renderer, 0.08f, 0.15f, 0.85f, 1.0f);
    zcsr_gl_present(runtime->renderer);

    LOGI("solid-color frame presented");
}

static void zcsr_android_handle_cmd(struct android_app* state, int32_t cmd) {
    zcsr_android_runtime* runtime;

    if (!state) return;
    runtime = (zcsr_android_runtime*)state->userData;
    if (!runtime) return;

    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (state->window) {
            zcsr_android_start_gl(runtime, state->window);
        }
        break;

    case APP_CMD_TERM_WINDOW:
        /* EGL surface/context must go away before ANativeWindow is detached. */
        zcsr_android_stop_gl(runtime);
        zcsr_surface_detach_native(runtime->surface);
        LOGI("surface detached");
        break;

    default:
        break;
    }
}

void android_main(struct android_app* state) {
    zcsr_rect bounds = {0, 0, 100, 100};
    zcsr_surface* surface;
    zcsr_android_runtime runtime;

    LOGI("android boot ok");

    surface = zcsr_surface_create("zcsr_android", bounds);
    if (!surface) {
        LOGE("surface allocation failed");
        return;
    }

    runtime.surface = surface;
    runtime.renderer = 0;

    state->userData = &runtime;
    state->onAppCmd = zcsr_android_handle_cmd;

    LOGI("surface_create=%p", (void*)surface);
    LOGI("clock_now_ns=%llu",
         (unsigned long long)zcsr_clock_now_ns());

    /* Cover the case where a window is already available. */
    if (state->window) {
        zcsr_android_handle_cmd(state, APP_CMD_INIT_WINDOW);
    }

    while (state->destroyRequested == 0) {
        struct android_poll_source* source = 0;
        int events = 0;

        int ident = ALooper_pollOnce(
            -1, 0, &events, (void**)&source);

        if (ident == ALOOPER_POLL_ERROR) {
            LOGE("ALooper_pollOnce error; exiting loop");
            break;
        }

        if (source) {
            source->process(state, source);
        }
    }

    /* Free GL/EGL resources while the attached native window is still valid. */
    zcsr_android_stop_gl(&runtime);
    zcsr_surface_detach_native(surface);

    state->onAppCmd = 0;
    state->userData = 0;

    zcsr_surface_destroy(surface);
    LOGI("android_main exiting");
}
