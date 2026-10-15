#ifndef ZCSR_INPUT_EXT_ANDROID_H
#define ZCSR_INPUT_EXT_ANDROID_H
#if !defined(__ANDROID__)
#error "zcsr/input_ext_android.h is Android-only"
#endif
#include <android/input.h>
#include "zcsr/input_ext.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Returns 1 if the event was handled, otherwise 0. */
int32_t zcsr_input_ext_android_event(
    zcsr_input_ext* input,
    AInputEvent* event);
/* Release transient input state after focus/window interruption. */
void zcsr_input_ext_android_reset(zcsr_input_ext* input);
#ifdef __cplusplus
}
#endif
#endif /* ZCSR_INPUT_EXT_ANDROID_H */
