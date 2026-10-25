#ifndef ZCSR_WINDOW_SHELL_H
#define ZCSR_WINDOW_SHELL_H
/* integration/window.h — 独立 window 骨架 (shell) 的挂载接口.
 *
 * window.c owns the WHOLE app lifecycle: a normal OS window (taskbar icon,
 * Alt-Tab switchable, never topmost), a black home screen with categorized
 * main-menu buttons (Delta Force style: one big button per registered test
 * module, plus the built-in SETTINGS and EXIT), the complete settings panel
 * as an overlay (Video / Graphics / Audio — real window management), and the
 * single main loop. This header is the plug-in point; a standard contract id
 * under include/zcsr will be defined later, at which point this file moves.
 *
 * Usage (see testmain.c):
 *   static const zcsr_window_module mods[] = { {...light module...} };
 *   return zcsr_window_main(mods, 1, argc, argv);
 */
#include "zcsr/gl_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One pluggable test module = one big button on the home screen.
 * All callbacks may be NULL except frame. r is the shared renderer; cw/ch are
 * the current window client size (module must adapt, resize between frames). */
typedef struct zcsr_window_module {
    const char* title;                                              /* button label */
    void* user;                                                     /* module state */
    bool (*init)(zcsr_gl_renderer* r, int cw, int ch, void* user);  /* lazy, once */
    void (*shutdown)(zcsr_gl_renderer* r, void* user);              /* at app exit */
    void (*frame)(zcsr_gl_renderer* r, int cw, int ch, void* user); /* one frame */
    bool (*key)(int key, void* user);                /* key press; true = consumed */
} zcsr_window_module;

/* Boots window/renderer/inputs/settings, runs the single main loop until the
 * user exits, tears everything down. Returns the process exit code.
 * argv is forwarded so "autoquit" runs the headless self-test. */
int zcsr_window_main(const zcsr_window_module* modules, int module_count,
                     int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_WINDOW_SHELL_H */
