#ifndef ZCSR_INPUT_ANDROID_H
#define ZCSR_INPUT_ANDROID_H
/* Android NativeActivity key-event bridge for the common zcsr_input queue.
 * Text remains in input_ext (android.view.KeyEvent Unicode); this header only
 * feeds key down/up state, so Android is not a no-op input backend. */
#if defined(__ANDROID__)
#include <android/input.h>
#include "input.h"

bool zcsr_input_android_event(zcsr_input* input, AInputEvent* event);
/* Clear held keys after APP_CMD_LOST_FOCUS / TERM_WINDOW. */
void zcsr_input_android_reset(zcsr_input* input);
#endif

#endif /* ZCSR_INPUT_ANDROID_H */
