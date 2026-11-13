#ifndef ZCSR_UI_H
#define ZCSR_UI_H
/* ZCSR UI Runtime — top-level instance and node-tree API.
 * Single event queue, single draw list per instance. No heap in public ABI
 * beyond what zcsr_ui_create allocates internally. */
#include "zcsr/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ZCSR_UI ZCSR_UI;

typedef struct ZCSR_UIConfig {
    uint32_t max_nodes;
    uint32_t event_queue_capacity;
    uint32_t draw_command_capacity;
    float viewport_width;
    float viewport_height;
} ZCSR_UIConfig;

/* ---- Lifecycle ---- */
ZCSR_Result zcsr_ui_create(const ZCSR_UIConfig* config, ZCSR_UI** out_ui);
void        zcsr_ui_destroy(ZCSR_UI* ui);
ZCSR_Result zcsr_ui_set_viewport(ZCSR_UI* ui, float width, float height);

/* ---- Node tree ---- */
ZCSR_Result zcsr_node_create(ZCSR_UI* ui, ZCSR_ComponentId component_id,
                             ZCSR_NodeId parent_id, ZCSR_NodeId* out_node_id);
ZCSR_Result zcsr_node_destroy(ZCSR_UI* ui, ZCSR_NodeId node_id);

ZCSR_Result zcsr_node_set_rect(ZCSR_UI* ui, ZCSR_NodeId node_id, const ZCSR_Rect* local_rect);
ZCSR_Result zcsr_node_get_rect(const ZCSR_UI* ui, ZCSR_NodeId node_id, ZCSR_Rect* out_local_rect);
ZCSR_Result zcsr_node_get_world_rect(const ZCSR_UI* ui, ZCSR_NodeId node_id, ZCSR_Rect* out_world_rect);
ZCSR_Result zcsr_node_set_flags(ZCSR_UI* ui, ZCSR_NodeId node_id, uint32_t flags);
ZCSR_Result zcsr_node_get_flags(const ZCSR_UI* ui, ZCSR_NodeId node_id, uint32_t* out_flags);
ZCSR_Result zcsr_node_set_z_order(ZCSR_UI* ui, ZCSR_NodeId node_id, int32_t z_order);
ZCSR_Result zcsr_node_set_parent(ZCSR_UI* ui, ZCSR_NodeId node_id, ZCSR_NodeId new_parent_id);

ZCSR_Result zcsr_node_get_component_id(const ZCSR_UI* ui, ZCSR_NodeId node_id, ZCSR_ComponentId* out);
ZCSR_Result zcsr_node_get_parent(const ZCSR_UI* ui, ZCSR_NodeId node_id, ZCSR_NodeId* out);
ZCSR_Result zcsr_node_get_child_count(const ZCSR_UI* ui, ZCSR_NodeId node_id, uint32_t* out);
ZCSR_Result zcsr_node_get_child_at(const ZCSR_UI* ui, ZCSR_NodeId node_id, uint32_t index, ZCSR_NodeId* out);

/* ---- Focus ---- */
ZCSR_Result zcsr_ui_set_focus(ZCSR_UI* ui, ZCSR_NodeId node_id);
ZCSR_Result zcsr_ui_get_focus(const ZCSR_UI* ui, ZCSR_NodeId* out_node_id);

/* ---- Style colors (editor-adjustable) ---- */
typedef enum ZCSR_ColorRole {
    ZCSR_COLOR_BG = 0,
    ZCSR_COLOR_TEXT,
    ZCSR_COLOR_BORDER
} ZCSR_ColorRole;

ZCSR_Result zcsr_node_set_color(ZCSR_UI* ui, ZCSR_NodeId node_id,
                                ZCSR_ColorRole role, ZCSR_Color color);
ZCSR_Result zcsr_node_get_color(const ZCSR_UI* ui, ZCSR_NodeId node_id,
                                ZCSR_ColorRole role, ZCSR_Color* out_color);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_H */
