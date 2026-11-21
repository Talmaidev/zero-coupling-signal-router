/* tests/test_ui_runtime.c — UI fixed-pool contract:
 * instance pool exhaustion, node capacity, embedded text limit, draw list. */
#include "zcsr/ui.h"
#include "zcsr/ui_label.h"
#include "zcsr/ui_draw_v2.h"
#include "zcsr/ui_input.h"
#include "zcsr/ui_event.h"

#include <assert.h>
#include <string.h>

static ZCSR_UIConfig make_config(uint32_t max_nodes, uint32_t events, uint32_t draws)
{
    ZCSR_UIConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.max_nodes = max_nodes;
    cfg.event_queue_capacity = events;
    cfg.draw_command_capacity = draws;
    cfg.viewport_width = 800.0f;
    cfg.viewport_height = 600.0f;
    return cfg;
}

static void test_instance_pool(void)
{
    ZCSR_UIConfig cfg = make_config(32u, 16u, 64u);
    ZCSR_UI* uis[4] = { NULL, NULL, NULL, NULL };
    ZCSR_UI* extra = NULL;
    size_t i;

    /* The fixed UI instance pool currently has four slots.  Fill all four,
     * then verify the next acquisition fails cleanly. */
    for (i = 0; i < 4u; ++i) {
        assert(zcsr_ui_create(&cfg, &uis[i]) == ZCSR_OK);
        assert(uis[i] != NULL);
    }
    assert(zcsr_ui_create(&cfg, &extra) == ZCSR_ERR_OUT_OF_MEMORY);
    assert(extra == NULL);

    for (i = 0; i < 4u; ++i) {
        zcsr_ui_destroy(uis[i]);
    }
    assert(zcsr_ui_create(&cfg, &extra) == ZCSR_OK);
    assert(extra != NULL);
    zcsr_ui_destroy(extra);
}

static void test_capacity_rejection(void)
{
    ZCSR_UIConfig cfg = make_config(257u, 16u, 64u);
    ZCSR_UI* ui = NULL;
    assert(zcsr_ui_create(&cfg, &ui) == ZCSR_ERR_BUFFER_TOO_SMALL);
    assert(ui == NULL);

    cfg = make_config(32u, 129u, 64u);
    assert(zcsr_ui_create(&cfg, &ui) == ZCSR_ERR_BUFFER_TOO_SMALL);
    assert(ui == NULL);

    cfg = make_config(32u, 16u, 1025u);
    assert(zcsr_ui_create(&cfg, &ui) == ZCSR_ERR_BUFFER_TOO_SMALL);
    assert(ui == NULL);
}

static void test_node_capacity(void)
{
    ZCSR_UIConfig cfg = make_config(2u, 8u, 16u);
    ZCSR_UI* ui = NULL;
    ZCSR_NodeId first = ZCSR_NODE_INVALID;
    ZCSR_NodeId second = ZCSR_NODE_INVALID;
    ZCSR_NodeId third = ZCSR_NODE_INVALID;

    assert(zcsr_ui_create(&cfg, &ui) == ZCSR_OK);
    assert(ui != NULL);
    assert(zcsr_node_create(ui, ZCSR_COMPONENT_LABEL, ZCSR_NODE_INVALID, &first) == ZCSR_OK);
    assert(zcsr_node_create(ui, ZCSR_COMPONENT_LABEL, ZCSR_NODE_INVALID, &second) == ZCSR_OK);
    assert(zcsr_node_create(ui, ZCSR_COMPONENT_LABEL, ZCSR_NODE_INVALID, &third) == ZCSR_ERR_BUFFER_TOO_SMALL);
    assert(third == ZCSR_NODE_INVALID);
    zcsr_ui_destroy(ui);
}

static void test_embedded_text_limit(void)
{
    ZCSR_UIConfig cfg = make_config(4u, 8u, 32u);
    ZCSR_UI* ui = NULL;
    ZCSR_NodeId label = ZCSR_NODE_INVALID;
    ZCSR_Rect rect = { 10.0f, 10.0f, 300.0f, 20.0f };
    ZCSR_Rect out = { 0, 0, 0, 0 };
    char text[300];
    uint32_t draw_count = 0;

    memset(text, 'x', sizeof text - 1u);
    text[sizeof text - 1u] = '\0';

    assert(zcsr_ui_create(&cfg, &ui) == ZCSR_OK);
    assert(ui != NULL);
    assert(zcsr_node_create(ui, ZCSR_COMPONENT_LABEL, ZCSR_NODE_INVALID, &label) == ZCSR_OK);
    assert(zcsr_node_set_rect(ui, label, &rect) == ZCSR_OK);
    assert(zcsr_node_get_rect(ui, label, &out) == ZCSR_OK);
    assert(out.x == rect.x && out.y == rect.y);
    assert(out.width == rect.width && out.height == rect.height);

    assert(zcsr_label_set_text(ui, label, text, 255u) == ZCSR_OK);
    assert(zcsr_label_set_text(ui, label, text, 256u) == ZCSR_ERR_BUFFER_TOO_SMALL);

    assert(zcsr_ui_build_draw_list(ui) == ZCSR_OK);
    assert(zcsr_ui_get_draw_command_count(ui, &draw_count) == ZCSR_OK);
    assert(draw_count == 1u);
    zcsr_ui_destroy(ui);
}

static void test_parent_offset_world_rect(void)
{
    ZCSR_UIConfig cfg = make_config(8u, 16u, 64u);
    ZCSR_UI* ui = NULL;
    ZCSR_NodeId parent = ZCSR_NODE_INVALID;
    ZCSR_NodeId child = ZCSR_NODE_INVALID;
    ZCSR_Rect rect;
    ZCSR_Rect world;
    ZCSR_Input in;
    ZCSR_UIEvent ev;

    assert(zcsr_ui_create(&cfg, &ui) == ZCSR_OK);
    assert(ui != NULL);

    assert(zcsr_node_create(ui, ZCSR_COMPONENT_BUTTON, ZCSR_NODE_INVALID, &parent) == ZCSR_OK);
    rect = (ZCSR_Rect){ 100.0f, 50.0f, 300.0f, 200.0f };
    assert(zcsr_node_set_rect(ui, parent, &rect) == ZCSR_OK);

    assert(zcsr_node_create(ui, ZCSR_COMPONENT_BUTTON, parent, &child) == ZCSR_OK);
    rect = (ZCSR_Rect){ 10.0f, 10.0f, 40.0f, 20.0f };
    assert(zcsr_node_set_rect(ui, child, &rect) == ZCSR_OK);

    assert(zcsr_node_get_world_rect(ui, child, &world) == ZCSR_OK);
    assert(world.x == 110.0f && world.y == 60.0f);
    assert(world.width == 40.0f && world.height == 20.0f);

    memset(&in, 0, sizeof(in));
    in.type = ZCSR_INPUT_POINTER_DOWN;
    in.pointer_position = (ZCSR_Vec2){ 120.0f, 70.0f };
    in.pointer_button = ZCSR_POINTER_LEFT;
    assert(zcsr_ui_process_input(ui, &in) == ZCSR_OK);

    in.type = ZCSR_INPUT_POINTER_UP;
    assert(zcsr_ui_process_input(ui, &in) == ZCSR_OK);

    memset(&ev, 0, sizeof(ev));
    assert(zcsr_ui_poll_event(ui, &ev) == ZCSR_OK);
    assert(ev.type == ZCSR_EVENT_BUTTON_CLICKED);
    assert(ev.source_node == child);

    zcsr_ui_destroy(ui);
}

static void test_draw_order_and_world_rect(void)
{
    ZCSR_UIConfig cfg = make_config(8u, 16u, 64u);
    ZCSR_UI* ui = NULL;
    ZCSR_NodeId bottom = ZCSR_NODE_INVALID;
    ZCSR_NodeId top = ZCSR_NODE_INVALID;
    ZCSR_NodeId parent = ZCSR_NODE_INVALID;
    ZCSR_NodeId child = ZCSR_NODE_INVALID;
    ZCSR_Rect rect;
    ZCSR_DrawCommand cmd;
    uint32_t count = 0;
    uint32_t i;

    assert(zcsr_ui_create(&cfg, &ui) == ZCSR_OK);
    assert(ui != NULL);

    assert(zcsr_node_create(ui, ZCSR_COMPONENT_LABEL, ZCSR_NODE_INVALID, &bottom) == ZCSR_OK);
    rect = (ZCSR_Rect){ 0.0f, 0.0f, 50.0f, 10.0f };
    assert(zcsr_node_set_rect(ui, bottom, &rect) == ZCSR_OK);
    assert(zcsr_label_set_text(ui, bottom, "bottom", 6u) == ZCSR_OK);

    assert(zcsr_node_create(ui, ZCSR_COMPONENT_LABEL, ZCSR_NODE_INVALID, &top) == ZCSR_OK);
    rect = (ZCSR_Rect){ 0.0f, 20.0f, 50.0f, 10.0f };
    assert(zcsr_node_set_rect(ui, top, &rect) == ZCSR_OK);
    assert(zcsr_label_set_text(ui, top, "top", 3u) == ZCSR_OK);
    assert(zcsr_node_set_z_order(ui, top, 10) == ZCSR_OK);

    assert(zcsr_ui_build_draw_list(ui) == ZCSR_OK);
    assert(zcsr_ui_get_draw_command_count(ui, &count) == ZCSR_OK);
    assert(count == 2u);
    memset(&cmd, 0, sizeof(cmd));
    assert(zcsr_ui_get_draw_command(ui, 0u, &cmd) == ZCSR_OK);
    assert(cmd.source_node == bottom);
    assert(zcsr_ui_get_draw_command(ui, 1u, &cmd) == ZCSR_OK);
    assert(cmd.source_node == top);

    assert(zcsr_node_create(ui, ZCSR_COMPONENT_BUTTON, ZCSR_NODE_INVALID, &parent) == ZCSR_OK);
    rect = (ZCSR_Rect){ 100.0f, 50.0f, 300.0f, 200.0f };
    assert(zcsr_node_set_rect(ui, parent, &rect) == ZCSR_OK);

    assert(zcsr_node_create(ui, ZCSR_COMPONENT_LABEL, parent, &child) == ZCSR_OK);
    rect = (ZCSR_Rect){ 10.0f, 10.0f, 40.0f, 20.0f };
    assert(zcsr_node_set_rect(ui, child, &rect) == ZCSR_OK);
    assert(zcsr_label_set_text(ui, child, "child", 5u) == ZCSR_OK);

    assert(zcsr_ui_build_draw_list(ui) == ZCSR_OK);
    assert(zcsr_ui_get_draw_command_count(ui, &count) == ZCSR_OK);

    for (i = 0; i < count; i++) {
        assert(zcsr_ui_get_draw_command(ui, i, &cmd) == ZCSR_OK);
        if (cmd.source_node == child) {
            assert(cmd.rect.x == 110.0f && cmd.rect.y == 60.0f);
            assert(cmd.rect.width == 40.0f && cmd.rect.height == 20.0f);
            break;
        }
    }
    assert(i < count);

    zcsr_ui_destroy(ui);
}

static void test_z_order_hit_matches_draw(void)
{
    ZCSR_UIConfig cfg = make_config(8u, 16u, 64u);
    ZCSR_UI* ui = NULL;
    ZCSR_NodeId bottom = ZCSR_NODE_INVALID;
    ZCSR_NodeId top = ZCSR_NODE_INVALID;
    ZCSR_Rect rect;
    ZCSR_Input in;
    ZCSR_UIEvent ev;
    ZCSR_DrawCommand cmd;
    uint32_t count = 0;
    uint32_t i;
    uint32_t first_bottom = 0xFFFFFFFFu;
    uint32_t first_top = 0xFFFFFFFFu;

    assert(zcsr_ui_create(&cfg, &ui) == ZCSR_OK);
    assert(ui != NULL);

    assert(zcsr_node_create(ui, ZCSR_COMPONENT_BUTTON, ZCSR_NODE_INVALID, &bottom) == ZCSR_OK);
    rect = (ZCSR_Rect){ 10.0f, 10.0f, 100.0f, 100.0f };
    assert(zcsr_node_set_rect(ui, bottom, &rect) == ZCSR_OK);

    assert(zcsr_node_create(ui, ZCSR_COMPONENT_BUTTON, ZCSR_NODE_INVALID, &top) == ZCSR_OK);
    rect = (ZCSR_Rect){ 30.0f, 30.0f, 100.0f, 100.0f };
    assert(zcsr_node_set_rect(ui, top, &rect) == ZCSR_OK);
    assert(zcsr_node_set_z_order(ui, top, 10) == ZCSR_OK);

    memset(&in, 0, sizeof(in));
    in.type = ZCSR_INPUT_POINTER_DOWN;
    in.pointer_position = (ZCSR_Vec2){ 50.0f, 50.0f };
    in.pointer_button = ZCSR_POINTER_LEFT;
    assert(zcsr_ui_process_input(ui, &in) == ZCSR_OK);
    in.type = ZCSR_INPUT_POINTER_UP;
    assert(zcsr_ui_process_input(ui, &in) == ZCSR_OK);

    memset(&ev, 0, sizeof(ev));
    assert(zcsr_ui_poll_event(ui, &ev) == ZCSR_OK);
    assert(ev.type == ZCSR_EVENT_BUTTON_CLICKED);
    assert(ev.source_node == top);

    assert(zcsr_ui_build_draw_list(ui) == ZCSR_OK);
    assert(zcsr_ui_get_draw_command_count(ui, &count) == ZCSR_OK);
    for (i = 0; i < count; i++) {
        assert(zcsr_ui_get_draw_command(ui, i, &cmd) == ZCSR_OK);
        if (cmd.source_node == bottom && first_bottom == 0xFFFFFFFFu) first_bottom = i;
        if (cmd.source_node == top && first_top == 0xFFFFFFFFu) first_top = i;
    }
    assert(first_bottom != 0xFFFFFFFFu);
    assert(first_top != 0xFFFFFFFFu);
    assert(first_top > first_bottom);

    zcsr_ui_destroy(ui);
}

int main(void)
{
    test_instance_pool();
    test_capacity_rejection();
    test_node_capacity();
    test_embedded_text_limit();
    test_parent_offset_world_rect();
    test_draw_order_and_world_rect();
    test_z_order_hit_matches_draw();
    return 0;
}
