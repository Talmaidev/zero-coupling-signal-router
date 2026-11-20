/* Windows OS-message keyboard test.
 *
 * This is deliberately not a synthetic queue injection: it posts real
 * WM_KEYDOWN/WM_KEYUP messages to a Win32 surface and verifies that the
 * platform pump translates them through input_keymap into ZCSR_KEY_* events.
 * It covers the first half of the real chain:
 *   WM_KEYDOWN -> zcsr_input_pump -> zcsr_input_poll -> ZCSR_KEY_*
 */
#include "zcsr/input.h"
#include "zcsr/platform.h"

#include <stdio.h>
#include <windows.h>

static int fails;
#define CK(label, cond) do { if (!(cond)) { printf("FAIL: %s\n", label); ++fails; } } while (0)

static void post_key(HWND hwnd, UINT message, WPARAM vk) {
    if (!PostMessageA(hwnd, message, vk, 0)) {
        printf("FAIL: PostMessageA(%s, vk=%u)\n",
               message == WM_KEYDOWN ? "WM_KEYDOWN" : "WM_KEYUP",
               (unsigned)vk);
        ++fails;
    }
}

static int expect_event(zcsr_input* in, zcsr_keycode key, zcsr_key_action action) {
    zcsr_key_event ev;
    if (!zcsr_input_poll(in, &ev)) {
        printf("FAIL: expected key=%u action=%d, queue empty\n",
               (unsigned)key, (int)action);
        ++fails;
        return 0;
    }
    if (ev.key != key || ev.action != action || ev.text) {
        printf("FAIL: expected key=%u action=%d, got key=%u action=%d text=%d\n",
               (unsigned)key, (int)action, (unsigned)ev.key,
               (int)ev.action, ev.text ? 1 : 0);
        ++fails;
        return 0;
    }
    return 1;
}

int main(void) {
    zcsr_rect bounds = { 0, 0, 320, 120 };
    uint8_t storage[4096];
    zcsr_surface* surface;
    zcsr_input* in;
    HWND hwnd;

    surface = zcsr_surface_create_opaque("zcsr-input-win32-pump", bounds);
    if (!surface) {
        printf("SKIP: no Win32 surface (no interactive desktop)\n");
        return 77;
    }
    hwnd = (HWND)zcsr_surface_native_handle(surface);
    if (!hwnd) {
        printf("FAIL: Win32 surface has no HWND\n");
        zcsr_surface_destroy(surface);
        return 1;
    }
    /* The test only needs a message target, not a visible fullscreen window. */
    ShowWindow(hwnd, SW_HIDE);

    in = zcsr_input_init(storage, sizeof storage);
    CK("input init", in != NULL);
    if (!in) {
        zcsr_surface_destroy(surface);
        return 1;
    }

    post_key(hwnd, WM_KEYDOWN, VK_F5);
    zcsr_input_pump(in, surface);
    expect_event(in, ZCSR_KEY_F5, ZCSR_KEY_DOWN);
    CK("F5 is down after WM_KEYDOWN", zcsr_input_is_down(in, ZCSR_KEY_F5) == true);

    post_key(hwnd, WM_KEYUP, VK_F5);
    zcsr_input_pump(in, surface);
    expect_event(in, ZCSR_KEY_F5, ZCSR_KEY_UP);
    CK("F5 is up after WM_KEYUP", zcsr_input_is_down(in, ZCSR_KEY_F5) == false);

    post_key(hwnd, WM_KEYDOWN, VK_ESCAPE);
    post_key(hwnd, WM_KEYUP, VK_ESCAPE);
    zcsr_input_pump(in, surface);
    expect_event(in, ZCSR_KEY_ESCAPE, ZCSR_KEY_DOWN);
    expect_event(in, ZCSR_KEY_ESCAPE, ZCSR_KEY_UP);

    post_key(hwnd, WM_KEYDOWN, 'S');
    post_key(hwnd, WM_KEYUP, 'S');
    zcsr_input_pump(in, surface);
    expect_event(in, ZCSR_KEY_S, ZCSR_KEY_DOWN);
    expect_event(in, ZCSR_KEY_S, ZCSR_KEY_UP);

    post_key(hwnd, WM_KEYDOWN, VK_DELETE);
    post_key(hwnd, WM_KEYUP, VK_DELETE);
    zcsr_input_pump(in, surface);
    expect_event(in, ZCSR_KEY_DELETE, ZCSR_KEY_DOWN);
    expect_event(in, ZCSR_KEY_DELETE, ZCSR_KEY_UP);

    zcsr_surface_destroy(surface);
    if (fails) {
        printf("input_win32_pump: %d failure(s)\n", fails);
        return 1;
    }
    printf("input_win32_pump: PASS (WM_KEYDOWN/UP -> ZCSR_KEY_F5/ESC/S/DELETE)\n");
    return 0;
}
