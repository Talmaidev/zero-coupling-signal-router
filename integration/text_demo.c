/* zcsr glrender text demo — verifies real 8x8 bitmap font rendering.
 * Creates a window, draws several lines of text, holds for 5 seconds.
 */
#include "zcsr/gl_render.h"
#include "zcsr/platform.h"

#include <stdio.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
static void zcsr_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#  include <unistd.h>
static void zcsr_sleep_ms(int ms) { usleep((useconds_t)(ms * 1000)); }
#endif

int main(void) {
    static unsigned char renderer_buf[8 * 1024 * 1024];

    printf("zcsr text demo — 8x8 bitmap font verification\n");

    zcsr_surface* surface = zcsr_surface_create(
        "zcsr text demo", (zcsr_rect){ 0, 0, 640, 480 });
    if (!surface) {
        printf("FAIL: no window surface (headless)\n");
        return 1;
    }

    zcsr_gl_renderer* r = zcsr_gl_create(surface, renderer_buf, sizeof renderer_buf);
    if (!r) {
        printf("FAIL: GL renderer unavailable\n");
        zcsr_surface_destroy(surface);
        return 1;
    }
    zcsr_gl_set_vsync(r, false);

    printf("Renderer ready. Drawing text for 5 seconds...\n");

    for (int frame = 0; frame < 300; ++frame) {  /* ~5 sec at 60fps */
        zcsr_gl_begin(r);

        /* dark background via fill_rect (no public clear API) */
        zcsr_gl_fill_rect(r, 0, 0, 640, 480, 0.1f, 0.1f, 0.15f, 1.0f);

        /* white text, scale 3x (24px tall) */
        zcsr_gl_draw_text(r, "Hello, zcsr!",  40,  40, 3.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        zcsr_gl_draw_text(r, "The quick brown fox", 40,  90, 2.0f, 0.5f, 0.9f, 1.0f, 1.0f);
        zcsr_gl_draw_text(r, "jumps over 123 lazy dogs!", 40, 120, 2.0f, 1.0f, 0.8f, 0.3f, 1.0f);
        zcsr_gl_draw_text(r, "ABCDEFGHIJKLMNOP", 40, 170, 2.0f, 0.9f, 0.5f, 0.9f, 1.0f);
        zcsr_gl_draw_text(r, "QRSTUVWXYZ 0123456789", 40, 200, 2.0f, 0.9f, 0.5f, 0.9f, 1.0f);
        zcsr_gl_draw_text(r, "!@#$%^&*()_+-=[]{}|;:',.<>/?", 40, 230, 2.0f, 0.6f, 0.8f, 0.6f, 1.0f);
        zcsr_gl_draw_text(r, "line1\nline2\nline3 (newline test)", 40, 280, 2.0f, 1.0f, 1.0f, 0.5f, 1.0f);
        zcsr_gl_draw_text(r, "scale=1 (8px): Hello 123!", 40, 380, 1.0f, 0.7f, 0.7f, 0.7f, 1.0f);

        /* small font tests (scale < 1: integer-pixel aligned) */
        zcsr_gl_draw_text(r, "scale=0.8 (6px): BEST 0123456789 ABCDEFGH", 40, 410, 0.8f, 0.9f, 0.9f, 1.0f, 1.0f);
        zcsr_gl_draw_text(r, "scale=0.6 (5px): BEST 0123456789", 40, 432, 0.6f, 0.9f, 0.9f, 1.0f, 1.0f);
        zcsr_gl_draw_text(r, "scale=0.5 (4px): BEST 0123456789 (info-loss expected)", 40, 452, 0.5f, 0.7f, 0.7f, 0.7f, 1.0f);

        zcsr_gl_flush(r);
        zcsr_gl_present(r);
        zcsr_sleep_ms(16);
    }

    printf("Done. Check the window for readable text.\n");
    zcsr_gl_destroy(r);
    zcsr_surface_destroy(surface);
    return 0;
}
