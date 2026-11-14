/* ZCSR-004 contract: runtime_viewport is the single owner of reference
 * resolution fitting, letterbox/pillarbox computation, and coordinate
 * round-trips. Pure CPU, no GL/window. */
#include "zcsr/runtime_viewport.h"

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(cond, name) do { \
    if (!(cond)) { printf("FAIL %s (line %d)\n", (name), __LINE__); ++failures; } \
} while (0)

static int nearf(float a, float b, float eps) {
    return fabsf(a - b) <= eps;
}

int main(void) {
    zcsr_runtime_viewport vp;

    /* Same aspect as 1920x1080: no letterbox/pillarbox. */
    zcsr_runtime_viewport_init(&vp, 1920.0f, 1080.0f);
    zcsr_runtime_viewport_set_workspace(&vp, 100.0f, 50.0f, 1600.0f, 900.0f);
    CHECK(nearf(vp.scale, 1600.0f / 1920.0f, 0.0001f), "same-aspect scale");
    CHECK(nearf(vp.content_rect.x, 100.0f, 0.01f), "same-aspect x");
    CHECK(nearf(vp.content_rect.y, 50.0f, 0.01f), "same-aspect y");
    CHECK(nearf(vp.content_rect.w, 1600.0f, 0.01f), "same-aspect w");
    CHECK(nearf(vp.content_rect.h, 900.0f, 0.01f), "same-aspect h");
    CHECK(nearf(vp.letterbox_top, 0.0f, 0.01f), "same-aspect no top bar");
    CHECK(nearf(vp.letterbox_bottom, 0.0f, 0.01f), "same-aspect no bottom bar");
    CHECK(nearf(vp.pillarbox_left, 0.0f, 0.01f), "same-aspect no left bar");
    CHECK(nearf(vp.pillarbox_right, 0.0f, 0.01f), "same-aspect no right bar");

    /* 4:3 workspace: content is width-limited, top/bottom letterbox. */
    zcsr_runtime_viewport_set_workspace(&vp, 0.0f, 0.0f, 1200.0f, 900.0f);
    CHECK(nearf(vp.scale, 1200.0f / 1920.0f, 0.0001f), "4:3 scale");
    CHECK(nearf(vp.content_rect.w, 1200.0f, 0.01f), "4:3 content width");
    CHECK(nearf(vp.content_rect.h, 675.0f, 0.01f), "4:3 content height");
    CHECK(vp.letterbox_top > 0.0f, "4:3 top bar");
    CHECK(vp.letterbox_bottom > 0.0f, "4:3 bottom bar");
    CHECK(nearf(vp.letterbox_top, 112.5f, 0.01f), "4:3 top bar size");
    CHECK(nearf(vp.letterbox_bottom, 112.5f, 0.01f), "4:3 bottom bar size");

    /* 16:10 workspace is taller relative to 16:9: top/bottom bars appear. */
    zcsr_runtime_viewport_set_workspace(&vp, 0.0f, 0.0f, 1600.0f, 1000.0f);
    CHECK(nearf(vp.scale, 1600.0f / 1920.0f, 0.0001f), "16:10 scale");
    CHECK(vp.letterbox_top > 0.0f, "16:10 top bar");
    CHECK(vp.letterbox_bottom > 0.0f, "16:10 bottom bar");
    CHECK(nearf(vp.letterbox_top, vp.letterbox_bottom, 0.01f), "16:10 bars symmetric");

    /* 21:9 workspace: height-limited, left/right pillarbox. */
    zcsr_runtime_viewport_set_workspace(&vp, 0.0f, 0.0f, 2100.0f, 900.0f);
    CHECK(nearf(vp.scale, 900.0f / 1080.0f, 0.0001f), "21:9 scale");
    CHECK(vp.pillarbox_left > 0.0f, "21:9 left bar");
    CHECK(vp.pillarbox_right > 0.0f, "21:9 right bar");
    CHECK(nearf(vp.pillarbox_left, vp.pillarbox_right, 0.01f), "21:9 bars symmetric");

    /* Coordinate round-trip across the content rect. */
    {
        float dx, dy, lx, ly, rx, ry;
        zcsr_runtime_viewport_set_workspace(&vp, 100.0f, 50.0f, 1600.0f, 900.0f);
        zcsr_runtime_viewport_logic_to_display(&vp, 960.0f, 540.0f, &dx, &dy);
        CHECK(nearf(dx, 100.0f + 800.0f, 0.01f), "logic center -> display x");
        CHECK(nearf(dy, 50.0f + 450.0f, 0.01f), "logic center -> display y");
        zcsr_runtime_viewport_display_to_logic(&vp, dx, dy, &lx, &ly);
        CHECK(nearf(lx, 960.0f, 0.01f), "display -> logic x roundtrip");
        CHECK(nearf(ly, 540.0f, 0.01f), "display -> logic y roundtrip");
        zcsr_runtime_viewport_logic_to_display(&vp, lx, ly, &rx, &ry);
        CHECK(nearf(rx, dx, 0.01f), "logic -> display x roundtrip");
        CHECK(nearf(ry, dy, 0.01f), "logic -> display y roundtrip");
    }

    /* Hit test only inside content, not in letterbox. */
    zcsr_runtime_viewport_set_workspace(&vp, 0.0f, 0.0f, 1600.0f, 1000.0f);
    CHECK(zcsr_runtime_viewport_hit_content(&vp, 800.0f, 500.0f), "hit content center");
    CHECK(!zcsr_runtime_viewport_hit_content(&vp, 800.0f, 40.0f), "miss top letterbox");
    CHECK(!zcsr_runtime_viewport_hit_content(&vp, -10.0f, 500.0f), "miss outside left");

    /* Zero workspace degrades to zero content; no NaN. */
    zcsr_runtime_viewport_set_workspace(&vp, 10.0f, 20.0f, 0.0f, 0.0f);
    CHECK(vp.content_rect.w == 0.0f && vp.content_rect.h == 0.0f, "zero workspace content");
    CHECK(vp.scale == 0.0f, "zero workspace scale");

    if (failures == 0) {
        printf("test_runtime_viewport PASS\n");
        return 0;
    }
    printf("test_runtime_viewport FAIL (%d)\n", failures);
    return 1;
}
