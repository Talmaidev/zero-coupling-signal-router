#include "zcsr/input_ext.h"

#include <stdio.h>

int main(void) {
    unsigned char tiny[1];
    static unsigned char storage[16 * 1024];
    if (zcsr_input_ext_init(0, sizeof storage)) {
        printf("FAIL: null buffer accepted\n");
        return 1;
    }
    if (zcsr_input_ext_init(tiny, sizeof tiny)) {
        printf("FAIL: tiny buffer accepted\n");
        return 1;
    }

    zcsr_input_ext* in = zcsr_input_ext_init(storage, sizeof storage);
    if (!in) {
        printf("FAIL: init\n");
        return 1;
    }

    zcsr_input_ext_pump(in, 0);
    zcsr_mouse_state mouse = zcsr_input_ext_mouse(in);
    if (mouse.dx != 0 || mouse.dy != 0 || mouse.wheel != 0) {
        printf("FAIL: null pump should clear deltas\n");
        zcsr_input_ext_shutdown(in);
        return 1;
    }
    if (zcsr_input_ext_pad(in, -1).connected || zcsr_input_ext_pad(in, ZCSR_PAD_MAX).connected) {
        printf("FAIL: invalid pad index should be disconnected\n");
        zcsr_input_ext_shutdown(in);
        return 1;
    }

    /* Event-driven backends can deliver down+up between two pumps. The merge
     * step must preserve both edges for one frame; otherwise a fast click is
     * silently lost. */
    {
        zcsr_mouse_state edge = { 0 };
        zcsr_mouse_state_merge_event_edges(&edge, ZCSR_MOUSE_LEFT, 0);
        if (edge.pressed != ZCSR_MOUSE_LEFT || edge.released != 0) {
            printf("FAIL: press edge merge\n");
            zcsr_input_ext_shutdown(in);
            return 1;
        }
        zcsr_mouse_state_merge_event_edges(&edge, 0, ZCSR_MOUSE_LEFT);
        if (edge.released != ZCSR_MOUSE_LEFT) {
            printf("FAIL: release edge merge\n");
            zcsr_input_ext_shutdown(in);
            return 1;
        }
    }

    zcsr_input_ext_shutdown(in);
    printf("PASS: inputext logic\n");
    return 0;
}
