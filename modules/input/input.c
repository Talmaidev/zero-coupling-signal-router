/* Agent 1 (Game Core) — Input & Window: keyboard events, window close, 60FPS timer.
 * Includes ONLY shared contracts. */
#include "zcsr/clock.h"
#include "zcsr/input.h"
#include "zcsr/window_event.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { ZCSR_INPUT_QUEUE_CAP = 64, ZCSR_INPUT_KEY_MAX = 512 };
enum { ZCSR_CLOSE_SLOT_CAP = 8 };

struct zcsr_frame_timer {
    uint64_t frame_ns;
    uint64_t last_ns;
    uint64_t delta_ns;
};

struct zcsr_input {
    zcsr_key_event queue[ZCSR_INPUT_QUEUE_CAP];
    uint8_t key_down[ZCSR_INPUT_KEY_MAX];
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

static void zcsr_input_record_key(zcsr_input* input, zcsr_keycode key, zcsr_key_action action) {
    if (!input) return;
    if (key < ZCSR_INPUT_KEY_MAX) input->key_down[key] = (action == ZCSR_KEY_DOWN) ? 1u : 0u;
    zcsr_input_push(input, (zcsr_key_event){ key, action });
}

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
    return (vk <= 0xffffu) ? (zcsr_keycode)vk : 0;
}

static void zcsr_input_platform_init(zcsr_input* input, zcsr_surface* window) {
    (void)input;
    (void)window;
}

static void zcsr_input_platform_pump(zcsr_input* input, zcsr_surface* window) {
    HWND hwnd = (HWND)zcsr_surface_native_handle(window);
    MSG msg;
    if (!input || !hwnd) return;
    /* Use NULL hwnd to pump ALL thread messages (not just this window's).
     * This prevents surface_pump_events from stealing keyboard messages. */
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_CLOSE || msg.message == WM_QUIT) {
            input->close_requested = true;
            zcsr_record_close(window);
        } else if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            if ((msg.lParam & (1L << 30)) == 0) zcsr_input_record_key(input, zcsr_map_key(msg.wParam), ZCSR_KEY_DOWN);
        } else if (msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) {
            zcsr_input_record_key(input, zcsr_map_key(msg.wParam), ZCSR_KEY_UP);
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

#elif defined(__ANDROID__)
#include <time.h>
uint64_t zcsr_clock_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}
static void zcsr_input_platform_init(zcsr_input* input, zcsr_surface* window) {
    (void)input;
    (void)window;
}
static void zcsr_input_platform_pump(zcsr_input* input, zcsr_surface* window) {
    (void)input;
    (void)window;
}
#elif defined(__linux__)
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

/* evdev keycode -> zcsr_keycode: X11 keycode = evdev + 8, and the X11 backend
 * uses keycode directly as zcsr_keycode, so match that for Linux consistency. */
static zcsr_keycode zcsr_map_key(uint32_t evdev_key) {
    uint32_t x11_keycode = evdev_key + 8u;
    return (x11_keycode <= 0xffffu) ? (zcsr_keycode)x11_keycode : 0;
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
                          state == WL_KEYBOARD_KEY_STATE_PRESSED ? ZCSR_KEY_DOWN : ZCSR_KEY_UP);
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
    zcsr_input_record_key(input, (zcsr_keycode)keycode, is_down ? ZCSR_KEY_DOWN : ZCSR_KEY_UP);
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

#elif !defined(__linux__)
uint64_t zcsr_clock_now_ns(void) {
    return 0;
}

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
    if (!i || k >= ZCSR_INPUT_KEY_MAX) return false;
    return i->key_down[k] != 0u;
}

bool zcsr_window_close_requested(zcsr_surface* s) {
    return zcsr_take_close(s);
}
