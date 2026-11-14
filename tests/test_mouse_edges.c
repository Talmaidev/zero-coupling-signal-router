/* ZCSR-007 contract: mouse pressed/released edges are derived from the held
 * button mask and are one-frame only. Pure CPU: no OS window, no backend. */
#include "zcsr/input_ext.h"

#include <stdint.h>
#include <stdio.h>

static int failures;

#define CHECK(cond, name) do { \
    if (!(cond)) { printf("FAIL %s (line %d)\n", (name), __LINE__); ++failures; } \
} while (0)

int main(void) {
    zcsr_mouse_state m;
    uint8_t previous = 0u;

    m.x = 0; m.y = 0; m.dx = 0; m.dy = 0; m.wheel = 0;
    m.buttons = 0u;
    m.pressed = 0u;
    m.released = 0u;

    /* Idle: no edges. */
    zcsr_mouse_state_update_edges(&m, previous);
    CHECK(m.pressed == 0u, "idle pressed clear");
    CHECK(m.released == 0u, "idle released clear");
    previous = m.buttons;

    /* 0 -> LEFT: press edge once. */
    m.buttons = (uint8_t)ZCSR_MOUSE_LEFT;
    zcsr_mouse_state_update_edges(&m, previous);
    CHECK(m.pressed == (uint8_t)ZCSR_MOUSE_LEFT, "left pressed edge");
    CHECK(m.released == 0u, "left press no release");
    previous = m.buttons;

    /* Held: no repeated press edge. */
    m.buttons = (uint8_t)ZCSR_MOUSE_LEFT;
    zcsr_mouse_state_update_edges(&m, previous);
    CHECK(m.pressed == 0u, "held no repeat pressed");
    CHECK(m.released == 0u, "held no release");
    previous = m.buttons;

    /* LEFT -> 0: release edge once. */
    m.buttons = 0u;
    zcsr_mouse_state_update_edges(&m, previous);
    CHECK(m.pressed == 0u, "release no pressed");
    CHECK(m.released == (uint8_t)ZCSR_MOUSE_LEFT, "left released edge");
    previous = m.buttons;

    /* Simultaneous RIGHT+MIDDLE down: both edges. */
    m.buttons = (uint8_t)(ZCSR_MOUSE_RIGHT | ZCSR_MOUSE_MIDDLE);
    zcsr_mouse_state_update_edges(&m, previous);
    CHECK(m.pressed == (uint8_t)(ZCSR_MOUSE_RIGHT | ZCSR_MOUSE_MIDDLE),
          "right+middle pressed");
    CHECK(m.released == 0u, "right+middle no release");
    previous = m.buttons;

    /* LEFT down while RIGHT/MIDDLE held: only LEFT is a new press. */
    m.buttons = (uint8_t)(ZCSR_MOUSE_LEFT | ZCSR_MOUSE_RIGHT | ZCSR_MOUSE_MIDDLE);
    zcsr_mouse_state_update_edges(&m, previous);
    CHECK(m.pressed == (uint8_t)ZCSR_MOUSE_LEFT, "only new left pressed");
    CHECK(m.released == 0u, "no release while held");
    previous = m.buttons;

    /* All up: all released. */
    m.buttons = 0u;
    zcsr_mouse_state_update_edges(&m, previous);
    CHECK(m.pressed == 0u, "all up no press");
    CHECK(m.released == (uint8_t)(ZCSR_MOUSE_LEFT | ZCSR_MOUSE_RIGHT | ZCSR_MOUSE_MIDDLE),
          "all released");

    /* Null is a no-op, not a crash. */
    zcsr_mouse_state_update_edges(NULL, 0u);
    CHECK(1, "null state tolerated");

    /* Event-driven quick click: down+up both arrived between pumps, so the
     * held mask is already 0. The queued event edges must still surface for
     * exactly this frame, or a right-click opens nothing. */
    m.buttons = 0u;
    m.pressed = 0u;
    m.released = 0u;
    zcsr_mouse_state_merge_event_edges(&m, (uint8_t)ZCSR_MOUSE_RIGHT,
                                       (uint8_t)ZCSR_MOUSE_RIGHT);
    CHECK(m.pressed == (uint8_t)ZCSR_MOUSE_RIGHT,
          "quick click pressed survives zero held mask");
    CHECK(m.released == (uint8_t)ZCSR_MOUSE_RIGHT,
          "quick click released survives zero held mask");

    m.pressed = 0u;
    m.released = 0u;
    zcsr_mouse_state_merge_event_edges(&m, 0xFFu, 0xFFu);
    CHECK(m.pressed == (uint8_t)(ZCSR_MOUSE_LEFT | ZCSR_MOUSE_RIGHT | ZCSR_MOUSE_MIDDLE),
          "event edges masked to known buttons");
    CHECK(m.released == (uint8_t)(ZCSR_MOUSE_LEFT | ZCSR_MOUSE_RIGHT | ZCSR_MOUSE_MIDDLE),
          "event released masked to known buttons");

    zcsr_mouse_state_merge_event_edges(NULL, 0u, 0u);
    CHECK(1, "null event merge tolerated");

    if (failures == 0) {
        printf("test_mouse_edges PASS\n");
        return 0;
    }
    printf("test_mouse_edges FAIL (%d)\n", failures);
    return 1;
}
