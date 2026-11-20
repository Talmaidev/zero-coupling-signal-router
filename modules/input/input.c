/*  — Input & Window: keyboard events, window close, 60FPS timer.
 * Includes ONLY shared contracts. */
#include "zcsr/clock.h"
#include "zcsr/input.h"
#include "zcsr/input_keymap.h"
#include "zcsr/window_event.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { ZCSR_INPUT_QUEUE_CAP = 64, ZCSR_CLOSE_SLOT_CAP = 8 };
_Static_assert(ZCSR_KEY_COUNT <= 256, "key_down array indexes must fit uint8_t");

struct zcsr_frame_timer {
    uint64_t frame_ns;
    uint64_t last_ns;
    uint64_t delta_ns;
};

struct zcsr_input {
    zcsr_key_event queue[ZCSR_INPUT_QUEUE_CAP];
    uint8_t key_down[ZCSR_KEY_COUNT];
    uint8_t head;
    uint8_t tail;
    bool close_requested;
    bool pump_initialized;
};

typedef struct {
    const zcsr_surface* surface;
    bool requested;
} zcsr_close_slot;

static zcsr_close_slot zcsr_close_slots[ZCSR_CLOSE_SLOT_CAP];

static void zcsr_record_close(zcsr_surface* surface) {
    size_t free_slot = ZCSR_CLOSE_SLOT_CAP;
    if (!surface) return;
    for (size_t i = 0; i < ZCSR_CLOSE_SLOT_CAP; ++i) {
        if (zcsr_close_slots[i].surface == surface) {
            zcsr_close_slots[i].requested = true;
            return;
        }
        if (!zcsr_close_slots[i].surface && free_slot == ZCSR_CLOSE_SLOT_CAP) free_slot = i;
    }
    if (free_slot < ZCSR_CLOSE_SLOT_CAP) {
        zcsr_close_slots[free_slot].surface = surface;
        zcsr_close_slots[free_slot].requested = true;
    }
}

static bool zcsr_take_close(zcsr_surface* surface) {
    if (!surface) return false;
    for (size_t i = 0; i < ZCSR_CLOSE_SLOT_CAP; ++i) {
        if (zcsr_close_slots[i].surface == surface) {
            bool requested = zcsr_close_slots[i].requested;
            zcsr_close_slots[i].requested = false;
            return requested;
        }
    }
    return false;
}

static bool zcsr_input_push(zcsr_input* input, zcsr_key_event event) {
    uint8_t next;
    if (!input) return false;
    next = (uint8_t)((input->tail + 1u) % ZCSR_INPUT_QUEUE_CAP);
    if (next == input->head) {
        input->head = (uint8_t)((input->head + 1u) % ZCSR_INPUT_QUEUE_CAP);
    }
    input->queue[input->tail] = event;
    input->tail = next;
    return true;
}

static void zcsr_input_record_key(zcsr_input* input, zcsr_keycode key, zcsr_key_action action,
                                  bool text) {
    if (!input) return;
    if (text) {
        /* Text is not a physical key. Win32 WM_CHAR carries a character, but
         * the public contract reserves `key` for ZCSR_KEY_* only. */
        key = ZCSR_KEY_NONE;
    } else {
        if (key == ZCSR_KEY_NONE || key >= ZCSR_KEY_COUNT) return;
        input->key_down[key] = (action == ZCSR_KEY_DOWN) ? 1u : 0u;
    }
    zcsr_input_push(input, (zcsr_key_event){ key, action, text });
}

bool zcsr_input_inject_key(zcsr_input* input, zcsr_keycode key, zcsr_key_action action) {
    if (!input || key == ZCSR_KEY_NONE || key >= ZCSR_KEY_COUNT) return false;
    zcsr_input_record_key(input, key, action, false);
    return true;
}

#if defined(_WIN32)  /* WM_KILLFOCUS 只在 Win32 后端使用；Linux 后端不定义它，避免 unused-function */
/* 焦点丢失时把所有按下的键视作松开。
 * 修的真实缺陷：打开系统对话框（Ctrl+O 的 GetOpenFileName）会跑一个模态消息循环，
 * 对话框拿走键盘焦点，Ctrl 的 KEYUP 被送进那个循环，本进程的队列永远收不到，
 * 于是 key_down[VK_CONTROL] 一直为 1，回到编辑器后所有可打印字符都被当快捷键吞掉
 * （表现为“属性框里打字没反应，但 F5 能用”）。 */
static void zcsr_input_release_all(zcsr_input* input) {
    if (!input) return;
    for (uint32_t k = 1u; k < (uint32_t)ZCSR_KEY_COUNT; ++k) {
        if (input->key_down[k]) {
            input->key_down[k] = 0u;
            zcsr_input_push(input, (zcsr_key_event){ (zcsr_keycode)k, ZCSR_KEY_UP, false });
        }
    }
}
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

/* Platform-owned WM_CLOSE flag (platform_win32.c). Kept as a C-ABI extern so
 * input does not include the platform module's private header. */
extern bool zcsr_platform_win32_take_close(zcsr_surface* s);

uint64_t zcsr_clock_now_ns(void) {
    LARGE_INTEGER freq;
    LARGE_INTEGER counter;
    uint64_t f;
    uint64_t c;
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&counter) || freq.QuadPart <= 0) return 0;
    f = (uint64_t)freq.QuadPart;
    c = (uint64_t)counter.QuadPart;
    return (c / f) * 1000000000ull + ((c % f) * 1000000000ull) / f;
}

static zcsr_keycode zcsr_map_key(WPARAM vk) {
    return zcsr_keycode_from_native(ZCSR_KEY_DOMAIN_WIN32_VK, (uint32_t)vk);
}

static void zcsr_input_platform_init(zcsr_input* input, zcsr_surface* window) {
    (void)input;
    (void)window;
}

static void zcsr_input_sync_modifiers(zcsr_input* input) {
    static const uint32_t mods[] = { 16u, 17u, 18u, 91u, 92u };   /* Shift/Ctrl/Alt/Win */
    for (size_t i = 0; i < sizeof(mods) / sizeof(mods[0]); ++i) {
        uint32_t vk = mods[i];
        zcsr_keycode key = zcsr_map_key((WPARAM)vk);
        if (key == ZCSR_KEY_NONE || !input->key_down[key]) continue;
        if ((GetAsyncKeyState((int)vk) & 0x8000) == 0) {
            zcsr_input_record_key(input, key, ZCSR_KEY_UP, false);
        }
    }
}

static void zcsr_input_platform_pump(zcsr_input* input, zcsr_surface* window) {
    HWND hwnd = (HWND)zcsr_surface_native_handle(window);
    MSG msg;
    bool packet_pending = false;
    if (!input || !hwnd) return;
    /* 每帧先按物理键盘状态校正修饰键。
     * 系统对话框（Ctrl+O / Save As / MessageBox）跑的是同一线程的模态循环，
     * Ctrl 的 KEYUP 会被那个循环消费掉，主循环永远收不到；只靠 WM_KILLFOCUS
     * 不够（owned dialog 不一定让 owner 失焦）。不校正的话 Ctrl 会“卡住”，
     * 之后所有可打印字符都被当成快捷键吞掉。 */
    zcsr_input_sync_modifiers(input);
    /* Use NULL hwnd to pump ALL thread messages (not just this window's).
     * This prevents surface_pump_events from stealing keyboard messages. */
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_CLOSE || msg.message == WM_QUIT) {
            input->close_requested = true;
            zcsr_record_close(window);
        } else if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            if (msg.wParam == VK_PACKET) {
                /* IME / SendInput(KEYEVENTF_UNICODE) / 辅助输入走的不是虚拟键码，
                 * 真正的字符在下一条 WM_CHAR 里。 */
                packet_pending = true;
            } else if ((msg.lParam & (1L << 30)) == 0) {
                zcsr_input_record_key(input, zcsr_map_key(msg.wParam), ZCSR_KEY_DOWN, false);
            }
        } else if (msg.message == WM_CHAR) {
            if (packet_pending && msg.wParam >= 32u && msg.wParam < 127u) {
                zcsr_input_record_key(input, ZCSR_KEY_NONE, ZCSR_KEY_DOWN, true);
            }
            packet_pending = false;
        } else if (msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) {
            if (msg.wParam == VK_PACKET) packet_pending = false;
            else zcsr_input_record_key(input, zcsr_map_key(msg.wParam), ZCSR_KEY_UP, false);
        } else if (msg.message == WM_KILLFOCUS ||
                   (msg.message == WM_ACTIVATE && LOWORD(msg.wParam) == WA_INACTIVE)) {
            /* 焦点离开本窗口（例如打开了系统文件对话框）时清空按键状态，
             * 否则错过 KEYUP 的修饰键（Ctrl/Shift）会一直保持按下。 */
            packet_pending = false;
            zcsr_input_release_all(input);
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

#elif defined(__ANDROID__)
#include <time.h>
#include <string.h>
#include <android/keycodes.h>
#include "zcsr/input_android.h"

uint64_t zcsr_clock_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

bool zcsr_input_android_event(zcsr_input* input, AInputEvent* event) {
    int32_t source, action, keycode;
    if (!input || !event) return false;
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_KEY) return false;
    source = AInputEvent_getSource(event);
    if ((source & (AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK |
                   AINPUT_SOURCE_DPAD)) != 0) {
        return false; /* gamepad/DPAD belongs to input_ext, not key state */
    }
    action = AKeyEvent_getAction(event);
    if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) {
        return false;
    }
    keycode = AKeyEvent_getKeyCode(event);
    if (keycode <= 0) return false;
    {
        zcsr_keycode key = zcsr_keycode_from_native(
            ZCSR_KEY_DOMAIN_ANDROID_KEYCODE, (uint32_t)keycode);
        if (key == ZCSR_KEY_NONE) return false;
        zcsr_input_record_key(
            input, key,
            action == AKEY_EVENT_ACTION_DOWN ? ZCSR_KEY_DOWN : ZCSR_KEY_UP,
            false);
    }
    return true;
}

void zcsr_input_android_reset(zcsr_input* input) {
    if (!input) return;
    memset(input->key_down, 0, sizeof(input->key_down));
}

static void zcsr_input_platform_init(zcsr_input* input, zcsr_surface* window) {
    (void)input;
    (void)window;
}
static void zcsr_input_platform_pump(zcsr_input* input, zcsr_surface* window) {
    (void)input;
    (void)window;
}
#elif defined(__linux__) && defined(ZCSR_LINUX_BACKEND_WAYLAND)
#include <time.h>
#include <wayland-client.h>
#include <string.h>
#include <unistd.h>

/* Internal accessors defined in platform_wayland.c (declared extern here
 * to avoid cross-module header include — zero-coupling guard). */
extern void* zcsr_platform_wayland_seat(const zcsr_surface* s);
extern bool  zcsr_platform_wayland_take_close(zcsr_surface* s);

uint64_t zcsr_clock_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

/* evdev key code -> ZCSR_KEY_*; X11 uses the same table after subtracting 8. */
static zcsr_keycode zcsr_map_key(uint32_t evdev_key) {
    return zcsr_keycode_from_native(ZCSR_KEY_DOMAIN_EVDEV, evdev_key);
}

/* ---- wl_keyboard listeners ---- */
static void kb_keymap(void* data, struct wl_keyboard* kb, uint32_t fmt, int32_t fd, uint32_t size) {
    (void)data; (void)kb; (void)fmt; (void)size;
    if (fd >= 0) close(fd);
}
static void kb_enter(void* data, struct wl_keyboard* kb, uint32_t serial,
                     struct wl_surface* surf, struct wl_array* keys) {
    (void)data; (void)kb; (void)serial; (void)surf; (void)keys;
}
static void kb_leave(void* data, struct wl_keyboard* kb, uint32_t serial, struct wl_surface* surf) {
    zcsr_input* input = data;
    (void)kb; (void)serial; (void)surf;
    if (input) memset(input->key_down, 0, sizeof(input->key_down));
}
static void kb_key(void* data, struct wl_keyboard* kb, uint32_t serial,
                   uint32_t time, uint32_t key, uint32_t state) {
    zcsr_input* input = data;
    (void)kb; (void)serial; (void)time;
    if (!input) return;
    zcsr_input_record_key(input, zcsr_map_key(key),
                          state == WL_KEYBOARD_KEY_STATE_PRESSED ? ZCSR_KEY_DOWN : ZCSR_KEY_UP, false);
}
static void kb_modifiers(void* data, struct wl_keyboard* kb, uint32_t serial,
                         uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    (void)data; (void)kb; (void)serial; (void)depressed; (void)latched; (void)locked; (void)group;
}
static void kb_repeat_info(void* data, struct wl_keyboard* kb, int32_t rate, int32_t delay) {
    (void)data; (void)kb; (void)rate; (void)delay;
}
static const struct wl_keyboard_listener kb_listener = {
    .keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave,
    .key = kb_key, .modifiers = kb_modifiers, .repeat_info = kb_repeat_info,
};

static struct wl_keyboard* g_wl_keyboard = NULL;

static void zcsr_input_platform_init(zcsr_input* input, zcsr_surface* window) {
    struct wl_seat* seat;
    if (!input || !window || input->pump_initialized) return;
    seat = (struct wl_seat*)zcsr_platform_wayland_seat(window);
    if (!seat) return;
    g_wl_keyboard = wl_seat_get_keyboard(seat);
    if (g_wl_keyboard) {
        wl_keyboard_add_listener(g_wl_keyboard, &kb_listener, input);
        input->pump_initialized = true;
    }
}

static void zcsr_input_platform_pump(zcsr_input* input, zcsr_surface* window) {
    /* Wayland events are dispatched by zcsr_surface_pump_events (platform module).
     * Keyboard events arrive via wl_keyboard listener callbacks above.
     * Here we only propagate the close flag from xdg_toplevel::close. */
    if (!input || !window) return;
    if (zcsr_platform_wayland_take_close(window)) {
        input->close_requested = true;
        zcsr_record_close(window);
    }
}

#elif defined(__linux__) && defined(ZCSR_LINUX_BACKEND_X11)
#include <time.h>
#include <string.h>

/* platform_x11.c 内部访问器（与 Wayland 分支同一零耦合约定：只 extern，
 * 不引入 modules/platform 的头）。 */
extern bool zcsr_platform_x11_next_key(zcsr_surface* s, unsigned int* keycode, int* is_down);
extern bool zcsr_platform_x11_take_close(zcsr_surface* s);
extern bool zcsr_platform_x11_is_focused(const zcsr_surface* s);

uint64_t zcsr_clock_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static void zcsr_input_platform_init(zcsr_input* input, zcsr_surface* window) {
    (void)input; (void)window;   /* X11 没有需要注册的监听器：事件由平台泵入环 */
}

static void zcsr_input_platform_pump(zcsr_input* input, zcsr_surface* window) {
    unsigned int keycode = 0;
    int down = 0;
    if (!input || !window) return;

    while (zcsr_platform_x11_next_key(window, &keycode, &down)) {
        zcsr_keycode key = zcsr_keycode_from_native(
            ZCSR_KEY_DOMAIN_X11_KEYCODE, keycode);
        zcsr_input_record_key(input, key,
                              down ? ZCSR_KEY_DOWN : ZCSR_KEY_UP, false);
    }

    /* 失焦时清空按下表，避免 Alt-Tab 之后按键粘住。 */
    if (!zcsr_platform_x11_is_focused(window)) {
        memset(input->key_down, 0, sizeof(input->key_down));
    }

    if (zcsr_platform_x11_take_close(window)) {
        input->close_requested = true;
        zcsr_record_close(window);
    }
}

#elif defined(__APPLE__)
#include <mach/mach_time.h>
#include "input_cocoa.h" /* same-module C-ABI shim over Cocoa NSEvent key pumping */

uint64_t zcsr_clock_now_ns(void) {
    static mach_timebase_info_data_t timebase;
    uint64_t now = mach_absolute_time();
    if (timebase.denom == 0) mach_timebase_info(&timebase);
    if (timebase.denom == 0) return 0;
    return (now * (uint64_t)timebase.numer) / (uint64_t)timebase.denom;
}

/* Trampoline so the Objective-C shim can feed key events into the module-private queue. */
static void zcsr_input_key_trampoline(void* ctx, uint16_t keycode, int is_down) {
    zcsr_input* input = (zcsr_input*)ctx;
    zcsr_keycode key = zcsr_keycode_from_native(
        ZCSR_KEY_DOMAIN_COCOA_KEYCODE, (uint32_t)keycode);
    if (key == ZCSR_KEY_NONE) return;
    zcsr_input_record_key(input, key, is_down ? ZCSR_KEY_DOWN : ZCSR_KEY_UP, false);
}

static void zcsr_input_platform_init(zcsr_input* input, zcsr_surface* window) {
    (void)input;
    (void)window;
}

static void zcsr_input_platform_pump(zcsr_input* input, zcsr_surface* window) {
    void* ns_window = zcsr_surface_native_handle(window);
    int closed = 0;
    if (!input || !ns_window) return;
    zcsr_cocoa_input_pump(ns_window, zcsr_input_key_trampoline, input, &closed);
    if (closed) {
        input->close_requested = true;
        zcsr_record_close(window);
    }
}

#else
#if defined(__linux__)
/* headless（ZCSR_LINUX_WINDOW_BACKEND=none）仍需要一个真实时钟，
 * 否则 frame timer 的 delta 恒为 0。 */
#include <time.h>
uint64_t zcsr_clock_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#else
uint64_t zcsr_clock_now_ns(void) {
    return 0;
}
#endif

static void zcsr_input_platform_init(zcsr_input* input, zcsr_surface* window) {
    (void)input;
    (void)window;
}

static void zcsr_input_platform_pump(zcsr_input* input, zcsr_surface* window) {
    (void)input;
    (void)window;
}
#endif

zcsr_frame_timer* zcsr_frame_timer_init(void* b, size_t n, uint32_t fps) {
    zcsr_frame_timer* timer;
    if (!b || n < sizeof(zcsr_frame_timer) || fps == 0) return 0;
    timer = (zcsr_frame_timer*)b;
    timer->frame_ns = 1000000000ull / (uint64_t)fps;
    if (timer->frame_ns == 0) timer->frame_ns = 1;
    timer->last_ns = zcsr_clock_now_ns();
    timer->delta_ns = 0;
    return timer;
}

bool zcsr_frame_timer_tick(zcsr_frame_timer* t) {
    uint64_t now;
    uint64_t elapsed;
    if (!t) return false;
    now = zcsr_clock_now_ns();
    elapsed = now - t->last_ns;
    if (elapsed < t->frame_ns) return false;
    t->delta_ns = elapsed;
    t->last_ns = now;
    return true;
}

uint64_t zcsr_frame_timer_delta_ns(const zcsr_frame_timer* t) {
    return t ? t->delta_ns : 0;
}

zcsr_input* zcsr_input_init(void* b, size_t n) {
    zcsr_input* input;
    if (!b || n < sizeof(zcsr_input)) return 0;
    input = (zcsr_input*)b;
    *input = (zcsr_input){ 0 };
    return input;
}

void zcsr_input_pump(zcsr_input* i, zcsr_surface* w) {
    if (!i || !w) return;
    zcsr_input_platform_init(i, w);
    zcsr_input_platform_pump(i, w);
}

bool zcsr_input_poll(zcsr_input* i, zcsr_key_event* o) {
    if (!i || !o || i->head == i->tail) return false;
    *o = i->queue[i->head];
    i->head = (uint8_t)((i->head + 1u) % ZCSR_INPUT_QUEUE_CAP);
    return true;
}

bool zcsr_input_is_down(const zcsr_input* i, zcsr_keycode k) {
    if (!i || k == ZCSR_KEY_NONE || k >= ZCSR_KEY_COUNT) return false;
    return i->key_down[k] != 0u;
}

bool zcsr_window_close_requested(zcsr_surface* s) {
    bool requested = zcsr_take_close(s);
#if defined(_WIN32)
    /* WM_CLOSE is recorded on the surface by the window proc; also drain it so
     * an app that only pumps surface events still sees the request exactly once. */
    if (zcsr_platform_win32_take_close(s)) requested = true;
#endif
    return requested;
}
