/* Engine-ext universal input extension.
 * Additive mouse/gamepad state beside the existing keyboard module. Includes only contracts plus
 * native backend headers. State and queues live entirely in the caller buffer.
 */
#include "zcsr/input_ext.h"
#include "inputext_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define ZCSR_MOUSE_BUTTON_MASK ((uint8_t)(ZCSR_MOUSE_LEFT | ZCSR_MOUSE_RIGHT | ZCSR_MOUSE_MIDDLE))

void zcsr_mouse_state_update_edges(zcsr_mouse_state* state, uint8_t previous_buttons) {
    uint8_t current;
    if (!state) return;
    current = (uint8_t)(state->buttons & ZCSR_MOUSE_BUTTON_MASK);
    previous_buttons = (uint8_t)(previous_buttons & ZCSR_MOUSE_BUTTON_MASK);
    state->pressed = (uint8_t)(current & (uint8_t)~previous_buttons);
    state->released = (uint8_t)(previous_buttons & (uint8_t)~current);
}

void zcsr_mouse_state_merge_event_edges(zcsr_mouse_state* state,
                                        uint8_t pressed, uint8_t released) {
    if (!state) return;
    state->pressed = (uint8_t)((state->pressed | pressed) & ZCSR_MOUSE_BUTTON_MASK);
    state->released = (uint8_t)((state->released | released) & ZCSR_MOUSE_BUTTON_MASK);
}

void zcsr_mouse_record_left_down(zcsr_input_ext* in, int x, int y, uint8_t mods) {
    if (!in) return;
    in->mouse.down_x = x;
    in->mouse.down_y = y;
    in->mouse.x = x;
    in->mouse.y = y;
    in->mouse.down_mods = (uint8_t)(mods & (ZCSR_MOD_CTRL | ZCSR_MOD_SHIFT |
                                            ZCSR_MOD_ALT | ZCSR_MOD_GUI));
    in->mouse.have_down = true;
    in->mouse_have_pos = true;
}

void zcsr_mouse_clear_down(zcsr_input_ext* in) {
    if (!in) return;
    in->mouse.have_down = false;
    in->mouse.down_mods = 0u;
    in->mouse.down_x = 0;
    in->mouse.down_y = 0;
}

/* ---- UTF-8 text queue (common to every backend with a producer) ---- */
static void zcsr_input_text_push_byte(zcsr_input_ext* in, unsigned char b) {
    if (!in || in->text_count >= sizeof(in->text_buf)) return;
    in->text_buf[in->text_count++] = (char)b;
}

void zcsr_input_text_push_utf8(zcsr_input_ext* in, const char* bytes, size_t n) {
    size_t i;
    if (!in || !bytes || n == 0u) return;
    /* Drop the whole chunk rather than split a multi-byte sequence. */
    if (n > sizeof(in->text_buf) - in->text_count) return;
    for (i = 0; i < n; ++i) {
        zcsr_input_text_push_byte(in, (unsigned char)bytes[i]);
    }
}

#if defined(_WIN32) || defined(__ANDROID__)
static void zcsr_input_text_push_codepoint(zcsr_input_ext* in, uint32_t cp) {
    char utf8[4];
    size_t n;
    if (!in) return;
    if (cp == '\r') cp = '\n';
    if (cp < 0x20u && cp != '\n' && cp != '\t') return;
    if (cp < 0x80u) {
        utf8[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800u) {
        utf8[0] = (char)(0xC0u | (cp >> 6));
        utf8[1] = (char)(0x80u | (cp & 0x3Fu));
        n = 2;
    } else if (cp < 0x10000u) {
        utf8[0] = (char)(0xE0u | (cp >> 12));
        utf8[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        utf8[2] = (char)(0x80u | (cp & 0x3Fu));
        n = 3;
    } else {
        utf8[0] = (char)(0xF0u | (cp >> 18));
        utf8[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        utf8[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        utf8[3] = (char)(0x80u | (cp & 0x3Fu));
        n = 4;
    }
    zcsr_input_text_push_utf8(in, utf8, n);
}

#if defined(_WIN32)
static void zcsr_input_text_push_utf16(zcsr_input_ext* in, uint16_t ch) {
    if (!in) return;
    if (ch >= 0xD800u && ch <= 0xDBFFu) {
        in->text_high_surrogate = ch;
        return;
    }
    if (ch >= 0xDC00u && ch <= 0xDFFFu) {
        if (in->text_high_surrogate) {
            uint32_t cp = 0x10000u +
                (((uint32_t)in->text_high_surrogate - 0xD800u) << 10) +
                ((uint32_t)ch - 0xDC00u);
            in->text_high_surrogate = 0;
            zcsr_input_text_push_codepoint(in, cp);
        }
        return;
    }
    in->text_high_surrogate = 0;
    zcsr_input_text_push_codepoint(in, (uint32_t)ch);
}
#endif /* _WIN32 */

#endif /* _WIN32 || __ANDROID__ */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <xinput.h>
#endif

#if defined(__ANDROID__)
#include <android/input.h>
#include <android/native_activity.h>
#include <jni.h>
#include <math.h>
#ifndef AMETA_CTRL_ON
#define AMETA_CTRL_ON 0x00001000
#endif
#ifndef AMETA_ALT_ON
#define AMETA_ALT_ON 0x00000002
#endif

static JNIEnv* zcsr_android_attach_env(zcsr_input_ext* in, bool* attached) {
    JavaVM* vm;
    JNIEnv* env = NULL;
    if (attached) *attached = false;
    if (!in || !in->java_vm) return NULL;
    vm = (JavaVM*)in->java_vm;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return NULL;
        if (attached) *attached = true;
    }
    return env;
}

static void zcsr_android_release_jni(zcsr_input_ext* in) {
    bool attached = false;
    JNIEnv* env;
    if (!in || !in->java_vm) return;
    env = zcsr_android_attach_env(in, &attached);
    if (env && in->key_event_class) {
        (*env)->DeleteGlobalRef(env, (jobject)in->key_event_class);
    }
    if (attached && in->java_vm) {
        JavaVM* vm = (JavaVM*)in->java_vm;
        (*vm)->DetachCurrentThread(vm);
    }
    in->java_vm = NULL;
    in->key_event_class = NULL;
    in->key_event_ctor = NULL;
    in->key_event_unicode = NULL;
}

void zcsr_input_ext_android_bind_activity(
    zcsr_input_ext* in, ANativeActivity* activity) {
    JNIEnv* env;
    jclass local_class;
    bool attached = false;
    JavaVM* vm;
    if (!in || !activity || !activity->vm) return;
    zcsr_android_release_jni(in);

    /* android_main runs on the native_app_glue thread, not the Activity UI
     * thread. activity->env belongs to the UI thread and must never be used
     * here; attach the native thread and let JNI resolve KeyEvent there. */
    vm = activity->vm;
    in->java_vm = vm;
    env = zcsr_android_attach_env(in, &attached);
    if (!env) {
        in->java_vm = NULL;
        return;
    }
    local_class = (*env)->FindClass(env, "android/view/KeyEvent");
    if (!local_class) {
        (*env)->ExceptionClear(env);
        if (attached) (*vm)->DetachCurrentThread(vm);
        in->java_vm = NULL;
        return;
    }
    in->key_event_class = (*env)->NewGlobalRef(env, local_class);
    (*env)->DeleteLocalRef(env, local_class);
    if (!in->key_event_class) {
        if (attached) (*vm)->DetachCurrentThread(vm);
        in->java_vm = NULL;
        return;
    }
    in->key_event_ctor = (void*)(*env)->GetMethodID(
        env, (jclass)in->key_event_class, "<init>", "(II)V");
    in->key_event_unicode = (void*)(*env)->GetMethodID(
        env, (jclass)in->key_event_class, "getUnicodeChar", "(I)I");
    if (!in->key_event_ctor || !in->key_event_unicode) {
        (*env)->ExceptionClear(env);
        zcsr_android_release_jni(in);
    }
    if (attached) (*vm)->DetachCurrentThread(vm);
}

/* Real keymap-aware Unicode conversion: android.view.KeyEvent uses the
 * framework's active keymap, unlike a hand-written US ASCII table. This is
 * the only text source available to a NativeActivity physical keyboard. */
static uint32_t zcsr_android_key_unicode(
    zcsr_input_ext* in, int32_t keycode, int32_t meta, int32_t action) {
    bool attached = false;
    JNIEnv* env;
    jobject event;
    jint cp;
    if (!in || !in->java_vm || !in->key_event_class ||
        !in->key_event_ctor || !in->key_event_unicode) return 0u;
    env = zcsr_android_attach_env(in, &attached);
    if (!env) return 0u;
    event = (*env)->NewObject(
        env, (jclass)in->key_event_class, (jmethodID)in->key_event_ctor,
        action, keycode);
    if (!event) {
        (*env)->ExceptionClear(env);
        if (attached && in->java_vm) {
            JavaVM* vm = (JavaVM*)in->java_vm;
            (*vm)->DetachCurrentThread(vm);
        }
        return 0u;
    }
    cp = (*env)->CallIntMethod(
        env, event, (jmethodID)in->key_event_unicode, meta);
    (*env)->DeleteLocalRef(env, event);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        cp = 0;
    }
    if (attached && in->java_vm) {
        JavaVM* vm = (JavaVM*)in->java_vm;
        (*vm)->DetachCurrentThread(vm);
    }
    return cp > 0 ? (uint32_t)cp : 0u;
}

void zcsr_scan_gamepads(zcsr_input_ext* in) {
    int i;
    if (!in) return;
    in->touch_pointer_id = -1;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        in->pad_device_id[i] = -1;
        in->pads[i] = (zcsr_pad_state){ 0 };
    }
}

void zcsr_close_gamepads(zcsr_input_ext* in) {
    int i;
    if (!in) return;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        in->pad_device_id[i] = -1;
        in->pads[i] = (zcsr_pad_state){ 0 };
    }
}

void zcsr_select_mouse_events(zcsr_input_ext* in, zcsr_surface* window) { (void)in; (void)window; }

void zcsr_pump_mouse(zcsr_input_ext* in, zcsr_surface* window) {
    (void)window;
    if (!in) return;
    in->mouse.dx = 0;
    in->mouse.dy = 0;
    in->mouse.wheel = 0;
}

void zcsr_pump_gamepads(zcsr_input_ext* in) { (void)in; }

static int zcsr_android_pad_slot(zcsr_input_ext* in, int32_t device_id) {
    int i, free_slot = -1;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        if (in->pad_device_id[i] == device_id) return i;
        if (free_slot < 0 && in->pad_device_id[i] < 0) free_slot = i;
    }
    if (free_slot < 0) return -1;
    in->pad_device_id[free_slot] = device_id;
    in->pads[free_slot].connected = true;
    return free_slot;
}

static void zcsr_android_set_button(zcsr_pad_state* pad, zcsr_pad_button button, int down) {
    uint16_t bit;
    if (!pad || button < 0 || button >= ZCSR_PAD_BUTTON_COUNT) return;
    bit = (uint16_t)(1u << (unsigned)button);
    if (down) pad->buttons = (uint16_t)(pad->buttons | bit);
    else pad->buttons = (uint16_t)(pad->buttons & (uint16_t)~bit);
}

static float zcsr_android_clamp_axis(float v, float lo, float hi) {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

int32_t zcsr_input_ext_android_event(
    zcsr_input_ext* in, AInputEvent* event) {
    int32_t type;
    int32_t source;
    if (!in || !event) return 0;
    type = AInputEvent_getType(event);
    source = AInputEvent_getSource(event);

    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t device_id, keycode, action, slot;
        zcsr_pad_button button;
        action = AKeyEvent_getAction(event);
        if ((source & (AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK |
                       AINPUT_SOURCE_DPAD)) == 0) {
            /* Text producer for a physical keyboard. NativeActivity does not
             * expose a soft-keyboard InputConnection; that remains an explicit
             * Android limitation, not a silent no-op. */
            if (action == AKEY_EVENT_ACTION_DOWN &&
                (AKeyEvent_getMetaState(event) &
                 (AMETA_CTRL_ON | AMETA_ALT_ON)) == 0) {
                int32_t meta = AKeyEvent_getMetaState(event);
                uint32_t cp = zcsr_android_key_unicode(
                    in, AKeyEvent_getKeyCode(event), meta, action);
                if (cp != 0u) {
                    zcsr_input_text_push_codepoint(in, cp);
                    return 1;
                }
            }
            return 0;
        }
        device_id = AInputEvent_getDeviceId(event);
        slot = zcsr_android_pad_slot(in, device_id);
        if (slot < 0) return 0;
        keycode = AKeyEvent_getKeyCode(event);
        switch (keycode) {
        case AKEYCODE_BUTTON_A:      button = ZCSR_PAD_A; break;
        case AKEYCODE_BUTTON_B:      button = ZCSR_PAD_B; break;
        case AKEYCODE_BUTTON_X:      button = ZCSR_PAD_X; break;
        case AKEYCODE_BUTTON_Y:      button = ZCSR_PAD_Y; break;
        case AKEYCODE_BUTTON_L1:     button = ZCSR_PAD_LB; break;
        case AKEYCODE_BUTTON_R1:     button = ZCSR_PAD_RB; break;
        case AKEYCODE_BUTTON_SELECT: button = ZCSR_PAD_BACK; break;
        case AKEYCODE_BUTTON_START:  button = ZCSR_PAD_START; break;
        case AKEYCODE_BUTTON_THUMBL: button = ZCSR_PAD_LSTICK; break;
        case AKEYCODE_BUTTON_THUMBR: button = ZCSR_PAD_RSTICK; break;
        case AKEYCODE_DPAD_UP:       button = ZCSR_PAD_DUP; break;
        case AKEYCODE_DPAD_DOWN:     button = ZCSR_PAD_DDOWN; break;
        case AKEYCODE_DPAD_LEFT:     button = ZCSR_PAD_DLEFT; break;
        case AKEYCODE_DPAD_RIGHT:    button = ZCSR_PAD_DRIGHT; break;
        default: return 0;
        }
        zcsr_android_set_button(&in->pads[slot], button, action == AKEY_EVENT_ACTION_DOWN);
        return 1;
    }

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        if ((source & AINPUT_SOURCE_TOUCHSCREEN) != 0) {
            int32_t action_full = AMotionEvent_getAction(event);
            int32_t action = action_full & AMOTION_EVENT_ACTION_MASK;
            int32_t ptr_index = (action_full & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
            switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
                in->touch_pointer_id = AMotionEvent_getPointerId(event, 0);
                in->touch_pending.x = AMotionEvent_getX(event, 0);
                in->touch_pending.y = AMotionEvent_getY(event, 0);
                in->touch_pending.active = true;
                in->touch_pending.pressed = true;
                break;
            case AMOTION_EVENT_ACTION_MOVE: {
                size_t count = AMotionEvent_getPointerCount(event);
                size_t i;
                for (i = 0; i < count; ++i) {
                    if (AMotionEvent_getPointerId(event, (int32_t)i) == in->touch_pointer_id) {
                        float nx = AMotionEvent_getX(event, (int32_t)i);
                        float ny = AMotionEvent_getY(event, (int32_t)i);
                        in->touch_pending.dx += nx - in->touch_pending.x;
                        in->touch_pending.dy += ny - in->touch_pending.y;
                        in->touch_pending.x = nx;
                        in->touch_pending.y = ny;
                        break;
                    }
                }
                break;
            }
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                break;
            case AMOTION_EVENT_ACTION_POINTER_UP:
                if (AMotionEvent_getPointerId(event, ptr_index) == in->touch_pointer_id) {
                    in->touch_pending.released = true;
                    in->touch_pending.active = false;
                    in->touch_pointer_id = -1;
                }
                break;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                in->touch_pending.released = true;
                in->touch_pending.active = false;
                in->touch_pointer_id = -1;
                break;
            default:
                return 0;
            }
            return 1;
        }
        if ((source & (AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK)) != 0) {
            int32_t device_id = AInputEvent_getDeviceId(event);
            int slot = zcsr_android_pad_slot(in, device_id);
            zcsr_pad_state* pad;
            float lx, ly, rx, ry, lt, rt;
            if (slot < 0) return 0;
            pad = &in->pads[slot];
            lx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            ly = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            rx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
            ry = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
            lt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
            rt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);
            /* Trigger fallback: some devices report triggers on Z/RZ. */
            if (lt <= 0.0f && rx > 0.0f) { lt = rx; rx = 0.0f; }
            if (rt <= 0.0f && ry > 0.0f) { rt = ry; ry = 0.0f; }
            if (fabsf(lx) < 0.15f) lx = 0.0f;
            if (fabsf(ly) < 0.15f) ly = 0.0f;
            if (fabsf(rx) < 0.15f) rx = 0.0f;
            if (fabsf(ry) < 0.15f) ry = 0.0f;
            pad->lx = zcsr_android_clamp_axis(lx, -1.0f, 1.0f);
            pad->ly = zcsr_android_clamp_axis(-ly, -1.0f, 1.0f);
            pad->rx = zcsr_android_clamp_axis(rx, -1.0f, 1.0f);
            pad->ry = zcsr_android_clamp_axis(-ry, -1.0f, 1.0f);
            pad->lt = zcsr_android_clamp_axis(lt, 0.0f, 1.0f);
            pad->rt = zcsr_android_clamp_axis(rt, 0.0f, 1.0f);
            return 1;
        }
        return 0;
    }
    return 0;
}

void zcsr_input_ext_android_reset(zcsr_input_ext* in) {
    int i;
    if (!in) return;
    in->touch_pending.active = false;
    in->touch_pending.pressed = false;
    in->touch_pending.released = true;
    in->touch_pointer_id = -1;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        in->pads[i].buttons = 0;
        in->pads[i].lx = in->pads[i].ly = in->pads[i].rx = in->pads[i].ry = 0.0f;
        in->pads[i].lt = in->pads[i].rt = 0.0f;
    }
}

#elif defined(_WIN32)

static bool zcsr_window_has_focus(HWND hwnd) {
    HWND foreground;

    if (!hwnd) return false;

    foreground = GetForegroundWindow();

    return foreground == hwnd ||
        (foreground != NULL &&
         IsChild(hwnd, foreground) != FALSE);
}

static float zcsr_norm_thumb(SHORT value) {
    if (value < 0) {
        return (float)value / 32768.0f;
    }

    return (float)value / 32767.0f;
}

static float zcsr_norm_trigger(BYTE value) {
    return (float)value / 255.0f;
}

static void zcsr_set_button(
    zcsr_pad_state* pad,
    zcsr_pad_button button,
    bool down ) {
    uint16_t bit;

    if (!pad) return;

    if ((unsigned)button >= (unsigned)ZCSR_PAD_BUTTON_COUNT) {
        return;
    }

    bit = (uint16_t)(1U << (unsigned)button);

    if (down) {
        pad->buttons = (uint16_t)(pad->buttons | bit);
    } else {
        pad->buttons = (uint16_t)(
            pad->buttons & (uint16_t)~bit
        );
    }
}

void zcsr_scan_gamepads(zcsr_input_ext* in) {
    DWORD i;

    if (!in) return;

    for (i = 0; i < (DWORD)ZCSR_PAD_MAX; ++i) {
        XINPUT_STATE state;
        DWORD result;

        memset(&state, 0, sizeof(state));
        result = XInputGetState(i, &state);

        if (result != ERROR_SUCCESS) {
            in->pads[i] = (zcsr_pad_state){ 0 };
        } else {
            in->pads[i] = (zcsr_pad_state){ 0 };
            in->pads[i].connected = true;
        }
    }
}

void zcsr_close_gamepads(zcsr_input_ext* in) {
    DWORD i;

    if (!in) return;

    for (i = 0; i < (DWORD)ZCSR_PAD_MAX; ++i) {
        in->pads[i] = (zcsr_pad_state){ 0 };
    }
}

void zcsr_pump_gamepads(zcsr_input_ext* in) {
    DWORD i;

    if (!in) return;

    for (i = 0; i < (DWORD)ZCSR_PAD_MAX; ++i) {
        XINPUT_STATE state;
        zcsr_pad_state* pad = &in->pads[i];
        DWORD result;

        memset(&state, 0, sizeof(state));
        result = XInputGetState(i, &state);

        if (result != ERROR_SUCCESS) {
            *pad = (zcsr_pad_state){ 0 };
            continue;
        }

        pad->connected = true;
        pad->buttons = 0;

        pad->lx = zcsr_norm_thumb(state.Gamepad.sThumbLX);
        pad->ly = -zcsr_norm_thumb(state.Gamepad.sThumbLY);
        pad->rx = zcsr_norm_thumb(state.Gamepad.sThumbRX);
        pad->ry = -zcsr_norm_thumb(state.Gamepad.sThumbRY);

        pad->lt = zcsr_norm_trigger(state.Gamepad.bLeftTrigger);
        pad->rt = zcsr_norm_trigger(state.Gamepad.bRightTrigger);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0)
            zcsr_set_button(pad, ZCSR_PAD_A, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0)
            zcsr_set_button(pad, ZCSR_PAD_B, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0)
            zcsr_set_button(pad, ZCSR_PAD_X, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0)
            zcsr_set_button(pad, ZCSR_PAD_Y, true);

        if ((state.Gamepad.wButtons &
            XINPUT_GAMEPAD_LEFT_SHOULDER) != 0)
            zcsr_set_button(pad, ZCSR_PAD_LB, true);

        if ((state.Gamepad.wButtons &
            XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0)
            zcsr_set_button(pad, ZCSR_PAD_RB, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0)
            zcsr_set_button(pad, ZCSR_PAD_BACK, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0)
            zcsr_set_button(pad, ZCSR_PAD_START, true);

        if ((state.Gamepad.wButtons &
            XINPUT_GAMEPAD_LEFT_THUMB) != 0)
            zcsr_set_button(pad, ZCSR_PAD_LSTICK, true);

        if ((state.Gamepad.wButtons &
            XINPUT_GAMEPAD_RIGHT_THUMB) != 0)
            zcsr_set_button(pad, ZCSR_PAD_RSTICK, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0)
            zcsr_set_button(pad, ZCSR_PAD_DUP, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0)
            zcsr_set_button(pad, ZCSR_PAD_DDOWN, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0)
            zcsr_set_button(pad, ZCSR_PAD_DLEFT, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0)
            zcsr_set_button(pad, ZCSR_PAD_DRIGHT, true);
    }
}

static uint8_t zcsr_mouse_buttons(void) {
    uint8_t buttons = 0;
    SHORT state;

    /* 0x8000 = currently held, 0x0001 = pressed since the previous query. A
     * click can complete between two pumps, so honour both bits: otherwise a
     * fast right-click never reaches a menu that only opens on the press edge. */
    state = GetAsyncKeyState(VK_LBUTTON);
    if ((state & 0x8000) != 0) {
        buttons = (uint8_t)(buttons | (uint8_t)ZCSR_MOUSE_LEFT);
    }

    state = GetAsyncKeyState(VK_RBUTTON);
    if ((state & 0x8000) != 0) {
        buttons = (uint8_t)(buttons | (uint8_t)ZCSR_MOUSE_RIGHT);
    }

    state = GetAsyncKeyState(VK_MBUTTON);
    if ((state & 0x8000) != 0) {
        buttons = (uint8_t)(buttons | (uint8_t)ZCSR_MOUSE_MIDDLE);
    }

    return buttons;
}

static uint8_t zcsr_mouse_mods_win32(WPARAM wparam) {
    uint8_t mods = 0;
    if ((wparam & MK_CONTROL) != 0) mods |= ZCSR_MOD_CTRL;
    if ((wparam & MK_SHIFT) != 0) mods |= ZCSR_MOD_SHIFT;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) mods |= ZCSR_MOD_ALT;
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 ||
        (GetKeyState(VK_RWIN) & 0x8000) != 0) mods |= ZCSR_MOD_GUI;
    return mods;
}

static int zcsr_clamp_mouse_x(int x, LONG width) {
    if (width <= 0) return 0;
    if (x < 0) return 0;
    if (x >= (int)width) return (int)width - 1;

    return x;
}

static int zcsr_clamp_mouse_y(int y, LONG height) {
    if (height <= 0) return 0;
    if (y < 0) return 0;
    if (y >= (int)height) return (int)height - 1;

    return y;
}

static void zcsr_accumulate_wheel(
    zcsr_input_ext* in,
    int delta ) {
    int total;
    int ticks;

    if (!in) return;

    total = in->wheel_remainder + delta;
    ticks = total / WHEEL_DELTA;
    in->wheel_remainder = total % WHEEL_DELTA;

    if (ticks > 0 && in->wheel_accum > INT_MAX - ticks) {
        in->wheel_accum = INT_MAX;
    } else if (ticks < 0 &&
               in->wheel_accum < INT_MIN - ticks) {
        in->wheel_accum = INT_MIN;
    } else {
        in->wheel_accum += ticks;
    }
}

static LRESULT CALLBACK zcsr_input_mouse_subclass_proc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR subclass_id,
    DWORD_PTR ref_data ) {
    zcsr_input_ext* in =
        (zcsr_input_ext*)(uintptr_t)ref_data;

    (void)subclass_id;
    (void)wparam;

    /* --- mouse button events (event-driven edge detection) --- */
    if (in && in->mouse_subclass_hwnd == hwnd) {
        switch (message) {
            case WM_LBUTTONDOWN:
                in->mouse_btn_pressed |= ZCSR_MOUSE_LEFT;
                /* Record the press point and platform-independent modifiers
                 * before the cursor is allowed to move. */
                zcsr_mouse_record_left_down(
                    in,
                    (int)(short)LOWORD(lparam),
                    (int)(short)HIWORD(lparam),
                    zcsr_mouse_mods_win32(wparam));
                break;
            case WM_LBUTTONUP:
                in->mouse_btn_released |= ZCSR_MOUSE_LEFT;
                in->mouse.x = (int)(short)LOWORD(lparam);
                in->mouse.y = (int)(short)HIWORD(lparam);
                in->mouse_have_pos = true;
                /* The press origin is one gesture, not a permanent field: clear
                 * it on the matching release (and on focus loss below), so a
                 * consumer cannot mistake a stale press for a held drag. */
                zcsr_mouse_clear_down(in);
                break;
            case WM_RBUTTONDOWN:
                in->mouse_btn_pressed |= ZCSR_MOUSE_RIGHT;
                in->mouse.x = (int)(short)LOWORD(lparam);
                in->mouse.y = (int)(short)HIWORD(lparam);
                in->mouse_have_pos = true;
                break;
            case WM_RBUTTONUP:
                in->mouse_btn_released |= ZCSR_MOUSE_RIGHT;
                in->mouse.x = (int)(short)LOWORD(lparam);
                in->mouse.y = (int)(short)HIWORD(lparam);
                in->mouse_have_pos = true;
                break;
            case WM_MBUTTONDOWN:
                in->mouse_btn_pressed |= ZCSR_MOUSE_MIDDLE;
                in->mouse.x = (int)(short)LOWORD(lparam);
                in->mouse.y = (int)(short)HIWORD(lparam);
                in->mouse_have_pos = true;
                break;
            case WM_MBUTTONUP:
                in->mouse_btn_released |= ZCSR_MOUSE_MIDDLE;
                in->mouse.x = (int)(short)LOWORD(lparam);
                in->mouse.y = (int)(short)HIWORD(lparam);
                in->mouse_have_pos = true;
                break;
            case WM_CHAR:
            case WM_IME_CHAR:
                zcsr_input_text_push_utf16(in, (uint16_t)wparam);
                break;
            case WM_KILLFOCUS:
                in->mouse.buttons = 0;
                in->mouse_btn_pressed = 0;
                in->mouse_btn_released = 0;
                zcsr_mouse_clear_down(in);
                break;
            case WM_ACTIVATE:
                if (LOWORD(wparam) == WA_INACTIVE) {
                    in->mouse.buttons = 0;
                    in->mouse_btn_pressed = 0;
                    in->mouse_btn_released = 0;
                    zcsr_mouse_clear_down(in);
                }
                break;
        }
    }

    if (message == WM_MOUSEWHEEL &&
        in &&
        in->mouse_subclass_hwnd == hwnd &&
        zcsr_window_has_focus(hwnd)) {

        int delta = (int)(short)GET_WHEEL_DELTA_WPARAM(wparam);

        zcsr_accumulate_wheel(in, delta);
    }

    if (message == WM_NCDESTROY) {
        (void)RemoveWindowSubclass(
            hwnd,
            zcsr_input_mouse_subclass_proc,
            (UINT_PTR)(uintptr_t)in
        );

        if (in && in->mouse_subclass_hwnd == hwnd) {
            in->mouse_subclass_hwnd = NULL;
            in->mouse_inside = false;
            in->mouse_have_pos = false;
            in->mouse.have_down = false;
            in->wheel_accum = 0;
            in->wheel_remainder = 0;
        }
    }

    return DefSubclassProc(hwnd, message, wparam, lparam);
}

void zcsr_select_mouse_events(
    zcsr_input_ext* in,
    zcsr_surface* window ) {
    HWND hwnd = NULL;

    if (!in) return;

    if (window) {
        hwnd = (HWND)zcsr_surface_native_handle(window);
    }

    if (in->mouse_subclass_hwnd &&
        in->mouse_subclass_hwnd != hwnd) {

        (void)RemoveWindowSubclass(
            in->mouse_subclass_hwnd,
            zcsr_input_mouse_subclass_proc,
            (UINT_PTR)(uintptr_t)in
        );

        in->mouse_subclass_hwnd = NULL;
    }

    if (!hwnd) {
        in->mouse_inside = false;
        in->mouse_have_pos = false;
        in->mouse.buttons = 0;
        in->wheel_accum = 0;
        in->wheel_remainder = 0;
        return;
    }

    if (in->mouse_subclass_hwnd == hwnd) return;

    if (SetWindowSubclass(
            hwnd,
            zcsr_input_mouse_subclass_proc,
            (UINT_PTR)(uintptr_t)in,
            (DWORD_PTR)(uintptr_t)in)) {
        in->mouse_subclass_hwnd = hwnd;
    }
}

void zcsr_pump_mouse(
    zcsr_input_ext* in,
    zcsr_surface* window ) {
    HWND hwnd;
    POINT pt;
    RECT client;
    LONG width;
    LONG height;
    bool inside;
    int x;
    int y;

    if (!in) return;

    in->mouse.dx = 0;
    in->mouse.dy = 0;

    in->mouse.wheel = in->wheel_accum;
    in->wheel_accum = 0;

    in->mouse.buttons = 0;

    if (!window) {
        in->mouse_inside = false;
        in->mouse_btn_pressed = 0;
        in->mouse_btn_released = 0;
        return;
    }

    hwnd = (HWND)zcsr_surface_native_handle(window);
    if (!hwnd) {
        in->mouse_inside = false;
        in->mouse_btn_pressed = 0;
        in->mouse_btn_released = 0;
        return;
    }

    bool has_focus = zcsr_window_has_focus(hwnd);

    /* Always sample mouse position (even without focus) so hover tracking
     * works when the user clicks the window to focus it. */
    if (!GetClientRect(hwnd, &client)) {
        in->mouse_inside = false;
        return;
    }

    width = client.right - client.left;
    height = client.bottom - client.top;

    if (width <= 0 || height <= 0) {
        in->mouse_inside = false;
        in->mouse_have_pos = false;
        return;
    }

    if (!GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt)) {
        in->mouse_inside = false;
        return;
    }

    inside = PtInRect(&client, pt) != FALSE;

    x = zcsr_clamp_mouse_x(pt.x, width);
    y = zcsr_clamp_mouse_y(pt.y, height);

    if (!in->mouse_have_pos) {
        in->mouse.x = x;
        in->mouse.y = y;
        in->mouse_have_pos = true;
        in->mouse_inside = inside;
    } else if ((in->mouse_btn_pressed | in->mouse_btn_released) == 0u) {
        /* 只有没有按钮边沿时才用系统光标位置覆盖。按下/松开消息里的
         * lparam 是这次点击的真实落点；合成输入或光标未移动时，
         * GetCursorPos 可能仍是上一帧的位置，覆盖会把点击判到上一行。 */
        if (inside && in->mouse_inside) {
            in->mouse.dx = x - in->mouse.x;
            in->mouse.dy = y - in->mouse.y;
        }
        in->mouse.x = x;
        in->mouse.y = y;
        in->mouse_inside = inside;
    } else {
        in->mouse_inside = inside;
    }

    /* Button state: only report when focused, but merge event-driven edges
     * so a click that focuses the window is not lost. */
    if (has_focus) {
        in->mouse.buttons = zcsr_mouse_buttons() | in->mouse_btn_pressed;
    } else {
        /* If a press event arrived while unfocused, the window is likely
         * being clicked to focus — report it so the UI can react. */
        if (in->mouse_btn_pressed != 0) {
            in->mouse.buttons = in->mouse_btn_pressed;
        }
    }

    /* Leave mouse_btn_pressed/released for zcsr_input_ext_pump's common
     * edge-merge step. They are event-driven edges that may arrive between
     * two pumps; clearing them here loses a fast click whose down+up both
     * happened before the next pump. The wrapper clears them after merging. */
}

#elif defined(__linux__) && defined(ZCSR_LINUX_BACKEND_X11)

/* platform_x11.c 内部访问器：X11 的指针/按键状态由平台泵维护，
 * 这里只读取快照并把滚轮一次性取走（与 Win32 的 wheel_accum 语义一致）。 */
extern bool zcsr_platform_x11_mouse_state(zcsr_surface* s, int* x, int* y,
                                          unsigned int* buttons, int* wheel,
                                          int* down_x, int* down_y, int* have_down,
                                          unsigned int* down_mods);
extern int  zcsr_platform_x11_take_wheel(zcsr_surface* s);
extern size_t zcsr_platform_x11_take_text(zcsr_surface* s, char* out, size_t capacity);

static uint8_t zcsr_mods_from_x11(unsigned int mask) {
    uint8_t mods = 0;
    /* X11 modifier masks: Shift=1<<0, Control=1<<2, Mod1=1<<3 (Alt), Mod4=1<<6 (Super). */
    if ((mask & (1u << 0)) != 0) mods |= ZCSR_MOD_SHIFT;
    if ((mask & (1u << 2)) != 0) mods |= ZCSR_MOD_CTRL;
    if ((mask & (1u << 3)) != 0) mods |= ZCSR_MOD_ALT;
    if ((mask & (1u << 6)) != 0) mods |= ZCSR_MOD_GUI;
    return mods;
}

/* Linux gamepad entry points live in inputext_linux_gamepad.c. */
void zcsr_select_mouse_events(zcsr_input_ext* in, zcsr_surface* window) { (void)in; (void)window; }

void zcsr_pump_mouse(zcsr_input_ext* in, zcsr_surface* window) {
    int mx = 0, my = 0, down_x = 0, down_y = 0, have_down = 0;
    unsigned int buttons = 0, down_mods = 0;
    int rim = 0;
    if (!in) return;

    if (!window || !zcsr_platform_x11_mouse_state(window, &mx, &my, &buttons, &rim,
                                                  &down_x, &down_y, &have_down,
                                                  &down_mods)) {
        in->mouse.dx = 0;
        in->mouse.dy = 0;
        in->mouse.wheel = 0;
        in->mouse.buttons = 0;
        in->mouse_have_pos = false;
        zcsr_mouse_clear_down(in);
        return;
    }

    if (!in->mouse_have_pos) {
        in->mouse.dx = 0;
        in->mouse.dy = 0;
        in->mouse_have_pos = true;
    } else {
        in->mouse.dx = mx - in->mouse.x;
        in->mouse.dy = my - in->mouse.y;
    }
    in->mouse.x = mx;
    in->mouse.y = my;

    /* X11 平台用 (1u << button_number) 存掩码，button 1/2/3 = 左/中/右：
     *   1<<1 = 2 → 左键，1<<2 = 4 → 中键，1<<3 = 8 → 右键。
     * 引擎枚举是 LEFT=1 / RIGHT=2 / MIDDLE=4，这里做一次语义映射。 */
    in->mouse.buttons = (uint8_t)((buttons & 2u  ? ZCSR_MOUSE_LEFT   : 0)
                                | (buttons & 8u  ? ZCSR_MOUSE_RIGHT  : 0)
                                | (buttons & 4u  ? ZCSR_MOUSE_MIDDLE : 0));

    if (have_down) {
        in->mouse.down_x = down_x;
        in->mouse.down_y = down_y;
        in->mouse.down_mods = zcsr_mods_from_x11(down_mods);
        in->mouse.have_down = true;
    } else {
        zcsr_mouse_clear_down(in);
    }

    in->mouse.wheel = zcsr_platform_x11_take_wheel(window);
    (void)rim;
}

#elif defined(__linux__) && defined(ZCSR_LINUX_BACKEND_WAYLAND)
/* Backend entry points live in inputext_wayland.c and
 * inputext_linux_gamepad.c. Both translation units are compiled into
 * zcsr_inputext; generic stubs here would produce multiple definitions. */
#elif defined(__APPLE__)
/* Backend entry points live in inputext_cocoa.m. */
#else

void zcsr_scan_gamepads(zcsr_input_ext* in) { (void)in; }
void zcsr_close_gamepads(zcsr_input_ext* in) { (void)in; }
void zcsr_select_mouse_events(zcsr_input_ext* in, zcsr_surface* window) { (void)in; (void)window; }
void zcsr_pump_gamepads(zcsr_input_ext* in) { (void)in; }
void zcsr_pump_mouse(zcsr_input_ext* in, zcsr_surface* window) {
    if (in) {
        in->mouse.dx = 0;
        in->mouse.dy = 0;
        in->mouse.wheel = 0;
    }
    (void)window;
}
#endif

size_t zcsr_input_ext_take_text(zcsr_input_ext* in, char* out, size_t capacity) {
    size_t n = 0;
    size_t max;
    if (!in || !out || capacity == 0u) return 0u;
    max = capacity - 1u;
    while (n < in->text_count && n < max) {
        unsigned char c = (unsigned char)in->text_buf[n];
        size_t len;
        if (c < 0x80u) len = 1u;
        else if ((c & 0xE0u) == 0xC0u) len = 2u;
        else if ((c & 0xF0u) == 0xE0u) len = 3u;
        else if ((c & 0xF8u) == 0xF0u) len = 4u;
        else len = 1u; /* invalid lead byte: forward it, do not stall the queue */
        if (n + len > in->text_count) break;  /* incomplete tail: keep for next call */
        if (n + len > max) break;             /* would split a sequence */
        n += len;
    }
    if (n) memcpy(out, in->text_buf, n);
    out[n] = '\0';
    if (n < in->text_count) {
        memmove(in->text_buf, in->text_buf + n, in->text_count - n);
        in->text_count -= n;
    } else {
        in->text_count = 0u;
    }
    return n;
}

unsigned zcsr_input_ext_text_input_capabilities(void) {
#if defined(_WIN32) || defined(__APPLE__)
    return ZCSR_TEXT_INPUT_PHYSICAL | ZCSR_TEXT_INPUT_IME_COMMIT;
#elif defined(__ANDROID__)
    return ZCSR_TEXT_INPUT_PHYSICAL;
#elif defined(__linux__) && !defined(__ANDROID__)
#  if defined(ZCSR_LINUX_BACKEND_WAYLAND)
    /* Wayland builds also compile X11, but the Wayland text path is xkbcommit
     * only: no text-input protocol, no preedit. Be conservative for mixed
     * builds rather than overclaiming XIM on a Wayland surface. */
    return ZCSR_TEXT_INPUT_PHYSICAL;
#  elif defined(ZCSR_LINUX_BACKEND_X11)
    return ZCSR_TEXT_INPUT_PHYSICAL | ZCSR_TEXT_INPUT_IME_COMMIT;
#  else
    return ZCSR_TEXT_INPUT_PHYSICAL;
#  endif
#else
    return ZCSR_TEXT_INPUT_PHYSICAL;
#endif
}

bool zcsr_input_ext_soft_keyboard_available(void) {
    /* No backend owns a soft keyboard today: Android has no Java editor,
     * Wayland has no text-input protocol, and desktop backends only receive
     * physical-key commits. Returning true would be a capability lie. */
    return false;
}

zcsr_input_ext* zcsr_input_ext_init(void* buffer, size_t bytes) {
    zcsr_input_ext* in;
    if (!buffer || bytes < sizeof(zcsr_input_ext)) return 0;
    in = (zcsr_input_ext*)buffer;
    *in = (zcsr_input_ext){ 0 };
    zcsr_scan_gamepads(in);
    return in;
}

void zcsr_input_ext_shutdown(zcsr_input_ext* in) {
    if (!in) return;

    /* Let every backend release its native listener objects before the caller
     * buffer is zeroed. Wayland/Cocoa own wl_pointer/wl_keyboard/NSEvent
     * monitors here; leaving them alive after shutdown dereferences freed
     * caller state on the next dispatch. */
    zcsr_select_mouse_events(in, NULL);
    zcsr_close_gamepads(in);
#if defined(__ANDROID__)
    zcsr_android_release_jni(in);
#endif
    *in = (zcsr_input_ext){ 0 };
}

void zcsr_input_ext_pump(
    zcsr_input_ext* in,
    zcsr_surface* window ) {
    if (!in) return;

    if (window && (!in->initialized || in->surface != window)) {
        zcsr_select_mouse_events(in, window);

        in->surface = window;
        in->initialized = true;
    }

    if (window) {
        zcsr_pump_mouse(in, window);
    } else {
        zcsr_select_mouse_events(in, NULL);
        in->surface = NULL;
        in->initialized = false;
        in->mouse.dx = 0;
        in->mouse.dy = 0;
        in->mouse.wheel = 0;
        in->mouse.buttons = 0;
    }

#if defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_X11)
    /* X11 produces text through XIM/XIC while pumping the platform event
     * queue. Move it into the same UTF-8 queue used by Win32/Android/Cocoa. */
    if (window) {
        char text[64];
        size_t text_n;
        while ((text_n = zcsr_platform_x11_take_text(window, text, sizeof text)) > 0u) {
            zcsr_input_text_push_utf8(in, text, text_n);
        }
    }
#endif

    /* One-frame edges are derived in the common wrapper so every backend
     * exposes the same pressed/released contract. Event-driven backends
     * queue edges that can arrive between two pumps; a fast down+up leaves
     * buttons=0, so those edges must be merged, not discarded. */
    zcsr_mouse_state_update_edges(&in->mouse, in->mouse_prev_buttons);
    zcsr_mouse_state_merge_event_edges(&in->mouse, in->mouse_btn_pressed,
                                       in->mouse_btn_released);
    in->mouse_btn_pressed = 0;
    in->mouse_btn_released = 0;
    in->mouse_prev_buttons = in->mouse.buttons;

    zcsr_pump_gamepads(in);
#if defined(__ANDROID__)
    in->touch = in->touch_pending;
    in->touch_pending.pressed = false;
    in->touch_pending.released = false;
    in->touch_pending.dx = 0.0f;
    in->touch_pending.dy = 0.0f;
#endif
}

zcsr_mouse_state zcsr_input_ext_mouse(const zcsr_input_ext* in) {
    return in ? in->mouse : (zcsr_mouse_state){ 0 };
}

zcsr_pad_state zcsr_input_ext_pad(const zcsr_input_ext* in, int index) {
    if (!in || index < 0 || index >= ZCSR_PAD_MAX) return (zcsr_pad_state){ 0 };
    return in->pads[index];
}

zcsr_touch_state zcsr_input_ext_touch(const zcsr_input_ext* in) {
#if defined(__ANDROID__)
    return in ? in->touch : (zcsr_touch_state){ 0 };
#else
    (void)in;
    return (zcsr_touch_state){ 0 };
#endif
}
