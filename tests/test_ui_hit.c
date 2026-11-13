/* Contract test for flat-node hit testing and pointer capture. Pure CPU. */
#include "zcsr/ui_node.h"
#include "zcsr/ui_panel.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, name) do { \
    if (!(cond)) { printf("FAIL %s (line %d)\n", (name), __LINE__); ++failures; } \
} while (0)

static zcsr_ui_node make_node(zcsr_ui_id id, zcsr_ui_widget_type type,
                              float x, float y, float w, float h,
                              bool enabled, bool visible) {
    zcsr_ui_node n;
    memset(&n, 0, sizeof(n));
    n.desc.id = id;
    n.desc.type = type;
    n.desc.rect.x = x;
    n.desc.rect.y = y;
    n.desc.rect.w = w;
    n.desc.rect.h = h;
    n.desc.enabled = enabled;
    n.desc.visible = visible;
    return n;
}

int main(void) {
    zcsr_ui_node nodes[3];
    zcsr_ui_context ctx;
    zcsr_ui_id hit;

    memset(&ctx, 0, sizeof(ctx));
    nodes[0] = make_node(1u, ZCSR_UI_PANEL, 0.0f, 0.0f, 200.0f, 100.0f, true, true);
    nodes[1] = make_node(10u, ZCSR_UI_BUTTON, 10.0f, 10.0f, 40.0f, 20.0f, true, true);
    nodes[2] = make_node(20u, ZCSR_UI_BUTTON, 20.0f, 20.0f, 40.0f, 20.0f, true, true);

    hit = zcsr_ui_hit_test(nodes, 3u, 30.0f, 25.0f);
    CHECK(hit == 20u, "topmost overlapping button wins");
    CHECK(zcsr_ui_hit_test(nodes, 3u, 100.0f, 100.0f) == ZCSR_UI_ID_NONE,
          "empty area returns none");

    nodes[2].desc.enabled = false;
    CHECK(zcsr_ui_hit_test(nodes, 3u, 30.0f, 25.0f) == 10u,
          "disabled top button falls through");
    nodes[2].desc.enabled = true;
    nodes[2].desc.visible = false;
    CHECK(zcsr_ui_hit_test(nodes, 3u, 30.0f, 25.0f) == 10u,
          "hidden top button falls through");

    nodes[2].desc.visible = true;
    CHECK(zcsr_ui_pointer_down(&ctx, nodes, 3u, 30.0f, 25.0f) == 20u,
          "pointer down captures top button");
    CHECK(ctx.pressed_id == 20u, "pointer owner stored");
    CHECK(zcsr_ui_pointer_owner(&ctx) == 20u, "pointer owner query");
    CHECK(zcsr_ui_pointer_up(&ctx, nodes, 3u, 30.0f, 25.0f) == 20u,
          "release over same node returns owner");
    CHECK(ctx.pressed_id == ZCSR_UI_ID_NONE, "owner cleared");
    CHECK(zcsr_ui_pointer_owner(&ctx) == ZCSR_UI_ID_NONE, "owner query after release");

    CHECK(zcsr_ui_pointer_down(&ctx, nodes, 3u, 30.0f, 25.0f) == 20u,
          "second pointer down");
    CHECK(zcsr_ui_pointer_up(&ctx, nodes, 3u, 100.0f, 100.0f) == ZCSR_UI_ID_NONE,
          "release outside does not activate");
    CHECK(ctx.hot_id == ZCSR_UI_ID_NONE, "hover cleared outside");

    CHECK(zcsr_ui_pointer_down(&ctx, nodes, 3u, 100.0f, 100.0f) == ZCSR_UI_ID_NONE,
          "down on empty space captures none");
    CHECK(zcsr_ui_pointer_up(&ctx, nodes, 3u, 100.0f, 100.0f) == ZCSR_UI_ID_NONE,
          "up on empty space returns none");

    if (failures == 0) {
        printf("test_ui_hit PASS\n");
        return 0;
    }
    printf("test_ui_hit FAIL (%d)\n", failures);
    return 1;
}
