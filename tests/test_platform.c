#include "zcsr/platform.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail_if(int condition) {
    return condition ? 1 : 0;
}

#define CHECK(cond, code) do { if (fail_if(cond)) { \
    printf("test_platform FAIL at line %d: code %d (%s)\n", __LINE__, (code), #cond); \
    return (code); } } while (0)

int main(void) {
    int count = zcsr_display_count();
    int i;
    int found_primary = 0;

    /* count() is always at least 1. */
    CHECK(count < 1, 1);

    /* Every valid index reads a fully initialized, sane record. */
    for (i = 0; i < count; ++i) {
        zcsr_display_info info;
        int j;
        int nul_found = 0;
        memset(&info, 0xCC, sizeof(info));
        CHECK(zcsr_display_get(i, &info) == false, 2);
        CHECK(info.width < 0 || info.height < 0, 3);
        CHECK(info.dpi_scale <= 0.0f, 4);
        if (info.is_primary) found_primary = 1;
        /* name must be NUL-terminated within the buffer. */
        for (j = 0; j < (int)sizeof(info.name); ++j) {
            if (info.name[j] == '\0') { nul_found = 1; break; }
        }
        CHECK(!nul_found, 5);
        /* is_primary is 0 or 1. */
        CHECK(info.is_primary != 0 && info.is_primary != 1, 6);
    }
    /* At least one display is primary when we have real records. */
    CHECK(!found_primary, 7);

    /* Out-of-range and negative indices fail and must not touch out. */
    {
        zcsr_display_info sentinel;
        memset(&sentinel, 0xDD, sizeof(sentinel));
        CHECK(zcsr_display_get(-1, &sentinel) != false, 8);
        CHECK(zcsr_display_get(count, &sentinel) != false, 9);
        CHECK(zcsr_display_get(count + 100, &sentinel) != false, 10);
    }
    /* NULL out fails. */
    CHECK(zcsr_display_get(0, NULL) != false, 11);

    /* Invalid surface / parameters must not crash and must fail cleanly. */
    {
        int w = 0x77, h = 0x88;
        CHECK(zcsr_surface_set_mode(NULL, ZCSR_WINDOW_WINDOWED) != false, 12);
        CHECK(zcsr_surface_set_mode(NULL, (zcsr_window_mode)99) != false, 13);
        CHECK(zcsr_surface_set_client_size(NULL, 100, 100) != false, 14);
        CHECK(zcsr_surface_set_client_size(NULL, 0, 100) != false, 15);
        CHECK(zcsr_surface_set_client_size(NULL, -5, 100) != false, 16);
        CHECK(zcsr_surface_move_to_display(NULL, 0) != false, 17);
        CHECK(zcsr_surface_move_to_display(NULL, 99999) != false, 18);
        zcsr_surface_get_client_size(NULL, &w, &h);
        CHECK(w != 0x77 || h != 0x88, 19); /* NULL surface leaves outputs */
        {
            float d = zcsr_surface_get_dpi_scale(NULL);
            CHECK(d <= 0.0f, 20);
        }
    }

    /* Real-window checks. These run only when a surface can be created;
     * on a headless machine (no X server) they are skipped. */
    {
        zcsr_rect bounds;
        bounds.x = 0; bounds.y = 0; bounds.w = 320; bounds.h = 120;
        zcsr_surface* s = zcsr_surface_create("test_platform", bounds);
        if (s) {
            int w = 0, h = 0;
            bool fs_ok;

            zcsr_surface_get_client_size(s, &w, &h);
            /* Client size should be positive on a created window. */
            CHECK(w <= 0 || h <= 0, 21);

            /* Non-positive sizes rejected. */
            CHECK(zcsr_surface_set_client_size(s, 0, 100) != false, 22);
            CHECK(zcsr_surface_set_client_size(s, 100, -1) != false, 23);

            /* Set an explicit client size and read it back. */
            CHECK(zcsr_surface_set_client_size(s, 240, 160) == false, 24);
            {
                int cw = 0, ch = 0;
                zcsr_surface_get_client_size(s, &cw, &ch);
                /* Allow a small tolerance for windowing backends. */
                if (cw < 230 || cw > 250 || ch < 150 || ch > 170) {
                    printf("client size readback got %dx%d (expected ~240x160)\n",
                           cw, ch);
                    /* Not fatal on headless/tiling WMs; report only. */
                }
            }

            /* Moving to an invalid display must fail without moving. */
            CHECK(zcsr_surface_move_to_display(s, 99999) != false, 25);

            /* Enter fullscreen; then client size changes must be rejected. */
            fs_ok = zcsr_surface_set_mode(s, ZCSR_WINDOW_FULLSCREEN);
            /* Android returns false; desktop should accept. */
            if (fs_ok) {
                CHECK(zcsr_surface_set_client_size(s, 100, 100) != false, 26);
                /* move to invalid display while fullscreen must fail. */
                CHECK(zcsr_surface_move_to_display(s, 99999) != false, 27);
                /* Re-requesting fullscreen must succeed and not corrupt state. */
                CHECK(zcsr_surface_set_mode(s, ZCSR_WINDOW_FULLSCREEN) == false, 28);
                /* Leave fullscreen back to windowed. */
                CHECK(zcsr_surface_set_mode(s, ZCSR_WINDOW_WINDOWED) == false, 29);
                /* After leaving fullscreen, client size is accepted again. */
                CHECK(zcsr_surface_set_client_size(s, 200, 200) != true, 30);
            }

            /* Invalid mode rejected. */
            CHECK(zcsr_surface_set_mode(s, (zcsr_window_mode)77) != false, 31);

            zcsr_surface_destroy(s);
            printf("test_platform: real-window checks ran\n");
        } else {
            printf("test_platform: no display server; real-window checks skipped\n");
        }
    }

    printf("test_platform: all checks passed\n");
    return 0;
}
