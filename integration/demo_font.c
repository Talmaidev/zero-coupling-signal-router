/* demo_font.c — TrueType font rendering demo.
 * Loads Inter.ttf, creates a font atlas, draws ASCII and Chinese text. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zcsr/platform.h"
#include "zcsr/gl_render.h"
#include "zcsr/font.h"
#include "zcsr/font_atlas.h"
#include "zcsr/input.h"
#include "zcsr/clock.h"

#define WIN_W 1024
#define WIN_H 768

static unsigned char renderer_buf[8 * 1024 * 1024];
static unsigned char input_buf[8 * 1024];
static unsigned char font_buf[1024 * 1024];
static unsigned char atlas_buf[8 * 1024 * 1024];

/* Embedded Inter.ttf */
#include "../tests/data/test_ttf.h"

int main(void) {
    zcsr_surface* window;
    zcsr_gl_renderer* renderer;
    zcsr_input* input;
    zcsr_font* font;
    zcsr_font_atlas* atlas;
    bool running = true;

    window = zcsr_surface_create("zcsr TTF Font Demo",
                                  (zcsr_rect){0, 0, WIN_W, WIN_H});
    if (!window) { fprintf(stderr, "surface create failed\n"); return 1; }

    renderer = zcsr_gl_create(window, renderer_buf, sizeof(renderer_buf));
    if (!renderer) { fprintf(stderr, "gl create failed\n"); return 1; }

    input = zcsr_input_init(input_buf, sizeof(input_buf));
    if (!input) { fprintf(stderr, "input init failed\n"); return 1; }

    font = zcsr_font_load(test_ttf, test_ttf_len, 32, font_buf, sizeof(font_buf));
    if (!font) { fprintf(stderr, "font load failed\n"); return 1; }

    atlas = zcsr_font_atlas_create(renderer, font, atlas_buf, sizeof(atlas_buf));
    if (!atlas) { fprintf(stderr, "atlas create failed\n"); return 1; }

    printf("TTF font loaded. Press ESC to quit.\n");

    while (running) {
        zcsr_key_event ev;
        zcsr_input_pump(input, window);
        zcsr_surface_pump_events(window);
        while (zcsr_input_poll(input, &ev)) {
            if (ev.action == ZCSR_KEY_DOWN && ev.key == 0x1B) running = false;
        }

        zcsr_gl_begin(renderer);
        zcsr_gl_clear(renderer, 0.1f, 0.1f, 0.15f, 1.0f);
        zcsr_font_atlas_begin_frame(atlas);

        /* Title */
        zcsr_font_draw_text(atlas, "ZCSR TrueType Font Demo", 40, 60, 1.5f,
                            1.0f, 0.9f, 0.3f, 1.0f);

        /* ASCII text at various sizes */
        zcsr_font_draw_text(atlas, "The quick brown fox jumps over the lazy dog",
                            40, 120, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        zcsr_font_draw_text(atlas, "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                            40, 160, 1.0f, 0.6f, 0.8f, 1.0f, 1.0f);
        zcsr_font_draw_text(atlas, "abcdefghijklmnopqrstuvwxyz",
                            40, 200, 1.0f, 0.6f, 0.8f, 1.0f, 1.0f);
        zcsr_font_draw_text(atlas, "0123456789 !@#$%^&*()",
                            40, 240, 1.0f, 0.8f, 1.0f, 0.6f, 1.0f);

        /* Small text */
        zcsr_font_draw_text(atlas, "Small text at scale 0.5",
                            40, 300, 0.5f, 0.7f, 0.7f, 0.7f, 1.0f);

        /* Large text */
        zcsr_font_draw_text(atlas, "LARGE 48px",
                            40, 360, 1.5f, 1.0f, 0.4f, 0.4f, 1.0f);

        /* Colored text */
        zcsr_font_draw_text(atlas, "Red", 40, 440, 1.2f, 1.0f, 0.2f, 0.2f, 1.0f);
        zcsr_font_draw_text(atlas, "Green", 140, 440, 1.2f, 0.2f, 1.0f, 0.2f, 1.0f);
        zcsr_font_draw_text(atlas, "Blue", 260, 440, 1.2f, 0.2f, 0.5f, 1.0f, 1.0f);

        /* Multi-line */
        zcsr_font_draw_text(atlas, "Line 1\nLine 2\nLine 3 with more text",
                            40, 520, 1.0f, 0.9f, 0.9f, 0.9f, 1.0f);

        /* 600 CJK characters — exercises atlas rebuild-on-overflow.
         * Codepoints U+4E00..U+4E00+599, rendered at small scale to fit. */
        {
            static char cjk_buf[600 * 3 + 1];
            int i, pos = 0;
            for (i = 0; i < 600; i++) {
                uint32_t cp = 0x4E00u + (uint32_t)i;
                /* UTF-8 encode: 3 bytes for CJK */
                cjk_buf[pos++] = (char)(0xE0 | ((cp >> 12) & 0x0F));
                cjk_buf[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                cjk_buf[pos++] = (char)(0x80 | (cp & 0x3F));
            }
            cjk_buf[pos] = '\0';
            zcsr_font_draw_text(atlas, cjk_buf,
                                40, 580, 0.6f, 0.8f, 0.8f, 1.0f, 1.0f);
        }

        /* Circle test (fill_circle) */
        zcsr_gl_fill_circle(renderer, 800, 150, 40, 64, 0.2f, 0.8f, 0.4f, 1.0f);
        zcsr_gl_fill_circle(renderer, 800, 250, 20, 32, 0.8f, 0.4f, 0.2f, 1.0f);
        zcsr_gl_fill_circle(renderer, 900, 200, 60, 128, 0.4f, 0.2f, 0.8f, 0.6f);

        zcsr_gl_flush(renderer);
        zcsr_gl_present(renderer);

        /* crude frame limiter */
        {
            uint64_t end = zcsr_clock_now_ns() + 16000000ull;
            while (zcsr_clock_now_ns() < end) { /* spin */ }
        }
    }

    zcsr_font_atlas_destroy(atlas);
    zcsr_font_destroy(font);
    zcsr_gl_destroy(renderer);
    zcsr_surface_destroy(window);
    printf("demo_font exited cleanly\n");
    return 0;
}
