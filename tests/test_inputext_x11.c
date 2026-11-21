#include "zcsr/platform.h"
#include "zcsr/input_ext.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
}

int main(void) {
    zcsr_rect bounds = {0, 0, 320, 120};
    zcsr_surface* s = zcsr_surface_create("inputext-x11-text", bounds);
    Display* dpy;
    Window win;
    int event_base = 0, error_base = 0, major = 0, minor = 0;
    static unsigned char storage[2048];
    zcsr_input_ext* in;
    char text[64];
    size_t n;

    if (!s) {
        printf("SKIP: no X11 display\n");
        return 0;
    }
    dpy = (Display*)zcsr_surface_native_display(s);
    win = (Window)(unsigned long)zcsr_surface_native_handle(s);
    if (!dpy || !win) {
        printf("FAIL: no native X11 handle\n");
        zcsr_surface_destroy(s);
        return 1;
    }
    if (!XTestQueryExtension(dpy, &event_base, &error_base, &major, &minor)) {
        printf("SKIP: XTest unavailable\n");
        zcsr_surface_destroy(s);
        return 0;
    }

    /* XMapWindow is asynchronous. XSetInputFocus before the window becomes
     * viewable returns BadMatch and leaves focus on the terminal, so XTEST
     * key events never reach this window. Pump until MapNotify is processed
     * (or a bounded wait expires), then focus the now-viewable window. */
    {
        int waited_ms = 0;
        XWindowAttributes attrs;
        while (waited_ms < 1000) {
            zcsr_surface_pump_events(s);
            if (XGetWindowAttributes(dpy, win, &attrs) &&
                attrs.map_state == IsViewable) {
                break;
            }
            sleep_ms(10);
            waited_ms += 10;
        }
    }
    XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
    XFlush(dpy);
    sleep_ms(30);
    zcsr_surface_pump_events(s);

    in = zcsr_input_ext_init(storage, sizeof storage);
    if (!in) {
        printf("FAIL: inputext init\n");
        zcsr_surface_destroy(s);
        return 1;
    }
    zcsr_input_ext_pump(in, s);

    XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_a), True, CurrentTime);
    XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_a), False, CurrentTime);
    XFlush(dpy);
    sleep_ms(30);
    zcsr_surface_pump_events(s);
    zcsr_input_ext_pump(in, s);

    n = zcsr_input_ext_take_text(in, text, sizeof text);
    if (n == 0u || strcmp(text, "a") != 0) {
        printf("FAIL: X11 key text n=%zu text='%s'\n", n, text);
        zcsr_input_ext_shutdown(in);
        zcsr_surface_destroy(s);
        return 1;
    }
    printf("PASS: X11 physical key text='%s'\n", text);

    /* P0 mouse contract: X11 must report the press origin and ZCSR_MOD_* bits,
     * and must clear have_down on the matching left release. XTest drives the
     * same X server events a real pointer would; this is not a mock. */
    {
        zcsr_mouse_state m;
        int screen = DefaultScreen(dpy);
        (void)screen;
        XWarpPointer(dpy, None, win, 0, 0, 0, 0, 40, 40);
        XSync(dpy, False);
        sleep_ms(30);
        zcsr_surface_pump_events(s);
        zcsr_input_ext_pump(in, s);

        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), True, CurrentTime);
        XSync(dpy, False);
        sleep_ms(20);
        zcsr_surface_pump_events(s);
        zcsr_input_ext_pump(in, s);

        XTestFakeButtonEvent(dpy, 1, True, CurrentTime);
        XSync(dpy, False);
        sleep_ms(20);
        zcsr_surface_pump_events(s);
        zcsr_input_ext_pump(in, s);
        m = zcsr_input_ext_mouse(in);
        if (!m.have_down || m.down_x < 30 || m.down_x > 50 ||
            m.down_y < 30 || m.down_y > 50 ||
            (m.down_mods & ZCSR_MOD_SHIFT) == 0u) {
            printf("FAIL: X11 press metadata have_down=%d down=(%d,%d) mods=0x%x\n",
                   m.have_down ? 1 : 0, m.down_x, m.down_y, (unsigned)m.down_mods);
            XTestFakeButtonEvent(dpy, 1, False, CurrentTime);
            XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), False, CurrentTime);
            zcsr_input_ext_shutdown(in);
            zcsr_surface_destroy(s);
            return 1;
        }

        XTestFakeButtonEvent(dpy, 1, False, CurrentTime);
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), False, CurrentTime);
        XSync(dpy, False);
        sleep_ms(20);
        zcsr_surface_pump_events(s);
        zcsr_input_ext_pump(in, s);
        m = zcsr_input_ext_mouse(in);
        if (m.have_down || (m.buttons & ZCSR_MOUSE_LEFT) != 0u ||
            (m.released & ZCSR_MOUSE_LEFT) == 0u) {
            printf("FAIL: X11 release did not clear down (have_down=%d buttons=0x%x released=0x%x)\n",
                   m.have_down ? 1 : 0, (unsigned)m.buttons, (unsigned)m.released);
            zcsr_input_ext_shutdown(in);
            zcsr_surface_destroy(s);
            return 1;
        }
        printf("PASS: X11 mouse press/release metadata\n");
    }

    zcsr_input_ext_shutdown(in);
    zcsr_surface_destroy(s);
    return 0;
}
