#ifndef ZCSR_UI_DRAW_V2_H
#define ZCSR_UI_DRAW_V2_H
/* ZCSR UI Runtime — abstract draw command list.
 * No GPU handles, no GL/VK/DX calls in this layer. A host backend consumes
 * the command list and turns it into real rendering. */
#include "zcsr/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ZCSR_DrawCommandType {
    ZCSR_DRAW_RECT,
    ZCSR_DRAW_BORDER,
    ZCSR_DRAW_TEXT,
    ZCSR_DRAW_IMAGE,
    ZCSR_DRAW_CLIP_PUSH,
    ZCSR_DRAW_CLIP_POP,
    ZCSR_DRAW_LINE
} ZCSR_DrawCommandType;

typedef struct ZCSR_DrawCommand {
    ZCSR_DrawCommandType type;
    ZCSR_NodeId          source_node;
    ZCSR_Rect            rect;
    ZCSR_Color           color;
    float                thickness;
    uint32_t             resource_key;
    const char*          text_utf8;
    uint32_t             text_bytes;
    ZCSR_Rect            uv_rect;
    int32_t              layer;
} ZCSR_DrawCommand;

typedef struct ZCSR_UI ZCSR_UI;

ZCSR_Result zcsr_ui_build_draw_list(ZCSR_UI* ui);
ZCSR_Result zcsr_ui_get_draw_command_count(const ZCSR_UI* ui, uint32_t* out_count);
ZCSR_Result zcsr_ui_get_draw_command(const ZCSR_UI* ui, uint32_t index, ZCSR_DrawCommand* out);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_DRAW_V2_H */
