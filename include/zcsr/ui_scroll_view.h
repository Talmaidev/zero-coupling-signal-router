#ifndef ZCSR_UI_SCROLL_VIEW_H
#define ZCSR_UI_SCROLL_VIEW_H
#include "zcsr/ui_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZCSR_UI ZCSR_UI;
ZCSR_Result zcsr_scroll_view_set_content_size(ZCSR_UI*, ZCSR_NodeId, float, float);
ZCSR_Result zcsr_scroll_view_set_offset(ZCSR_UI*, ZCSR_NodeId, float, float);
ZCSR_Result zcsr_scroll_view_get_offset(const ZCSR_UI*, ZCSR_NodeId, float*, float*);
ZCSR_Result zcsr_scroll_view_set_scroll_axes(ZCSR_UI*, ZCSR_NodeId, bool, bool);
ZCSR_Result zcsr_scroll_view_set_scrollbar_visibility(ZCSR_UI*, ZCSR_NodeId, bool, bool);
ZCSR_Result zcsr_scroll_view_set_scrollbar_thickness(ZCSR_UI*, ZCSR_NodeId, float);
ZCSR_Result zcsr_scroll_view_set_enabled(ZCSR_UI*, ZCSR_NodeId, bool);
#ifdef __cplusplus
}
#endif
#endif
