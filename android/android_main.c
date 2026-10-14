/* zcsr Android Phase 1A — native window lifecycle. */
#include <android_native_app_glue.h>
#include <android/log.h>
#include <stdint.h>
#include "zcsr/platform.h"
#include "zcsr/clock.h"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "zcsr", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "zcsr", __VA_ARGS__))

static void zcsr_android_handle_cmd(struct android_app* state, int32_t cmd) {
    zcsr_surface* surface;
    if (!state) return;
    surface = (zcsr_surface*)state->userData;
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (surface && state->window) {
            if (zcsr_surface_attach_native(surface, state->window)) {
                LOGI("surface attached: native_window=%p",
                     zcsr_surface_native_handle(surface));
            } else {
                LOGE("surface attach failed");
            }
        }
        break;
    case APP_CMD_TERM_WINDOW:
        if (surface) {
            zcsr_surface_detach_native(surface);
            LOGI("surface detached");
        }
        break;
    default:
        break;
    }
}

void android_main(struct android_app* state) {
    /* Android ignores bounds; the system creates the actual native window. */
    zcsr_rect bounds = {0, 0, 100, 100};
    zcsr_surface* surface;

    LOGI("android boot ok");

    surface = zcsr_surface_create("zcsr_android", bounds);
    if (!surface) {
        LOGE("surface allocation failed");
        return;
    }

    /* userData is application-owned. platform.c never accesses android_app. */
    state->userData = surface;
    state->onAppCmd = zcsr_android_handle_cmd;

    /* Cover the case where a window is already available. */
    if (state->window) {
        if (!zcsr_surface_attach_native(surface, state->window)) {
            LOGE("initial surface attach failed");
        }
    }

    LOGI("surface_create=%p", (void*)surface);
    LOGI("native_handle=%p", zcsr_surface_native_handle(surface));
    LOGI("native_display=%p", zcsr_surface_native_display(surface));
    LOGI("clock_now_ns=%llu",
         (unsigned long long)zcsr_clock_now_ns());

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

    /* Avoid leaving the callback with a pointer to a released surface. */
    state->onAppCmd = 0;
    state->userData = 0;
    zcsr_surface_destroy(surface);
    LOGI("android_main exiting");
}
