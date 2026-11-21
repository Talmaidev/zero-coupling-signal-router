#ifndef ZCSR_INPUT_EXT_ANDROID_H
#define ZCSR_INPUT_EXT_ANDROID_H
#if !defined(__ANDROID__)
#error "zcsr/input_ext_android.h is Android-only"
#endif
#include <android/input.h>
#include <android/native_activity.h>
#include "zcsr/input_ext.h"
/* Bind the NativeActivity JVM so physical keyboard events can be converted
 * through android.view.KeyEvent.getUnicodeChar with the active keymap.
 * Must be called after init and before the first input event. */
void zcsr_input_ext_android_bind_activity(
    zcsr_input_ext* input,
    ANativeActivity* activity);
/* Returns 1 if the event was handled, otherwise 0. */
int32_t zcsr_input_ext_android_event(
    zcsr_input_ext* input,
    AInputEvent* event);
/* Release transient input state after focus/window interruption. */
void zcsr_input_ext_android_reset(zcsr_input_ext* input);
#endif /* ZCSR_INPUT_EXT_ANDROID_H */
