/* zcsr Android Phase 0 — minimal native entry.
 * Goal: prove NDK build + NativeActivity boot + logcat "zcsr: android boot ok".
 * Exercises all 5 public platform APIs from zcsr/platform.h to prove the
 * T1 Android stubs link and run safely. No EGL/GLES, no touch, no audio. */
#include <android_native_app_glue.h>
#include <android/log.h>
#include "zcsr/platform.h"
#include "zcsr/clock.h"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "zcsr", __VA_ARGS__))

void android_main(struct android_app* state) {
    LOGI("android boot ok");

    /* Exercise all 5 platform public APIs — Android stubs must not crash. */
    zcsr_rect bounds = {0, 0, 100, 100};
    zcsr_surface* surf = zcsr_surface_create("zcsr_phase0", bounds);
    LOGI("surface_create=%p", (void*)surf);
    LOGI("native_handle=%p", zcsr_surface_native_handle(surf));
    LOGI("native_display=%p", zcsr_surface_native_display(surf));
    zcsr_surface_pump_events(surf);
    LOGI("clock_now_ns=%llu", (unsigned long long)zcsr_clock_now_ns());

    /* native_app_glue event loop: block on ALooper, exit on destroy. */
    while (state->destroyRequested == 0) {
        struct android_poll_source* source = NULL;
        int events = 0;

        int ident = ALooper_pollOnce(-1, NULL, &events, (void**)&source);
        if (ident == ALOOPER_POLL_ERROR) {
            LOGI("ALooper_pollOnce error; exiting loop");
            break;
        }

        if (source != NULL) {
            source->process(state, source);
        }
    }

    zcsr_surface_destroy(surf);
    LOGI("android_main exiting");
}
