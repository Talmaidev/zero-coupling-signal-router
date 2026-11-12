#ifndef ZCSR_UI_SLIDER_H
#define ZCSR_UI_SLIDER_H
#include "zcsr/ui_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZCSR_UI ZCSR_UI;
ZCSR_Result zcsr_slider_set_range(ZCSR_UI*, ZCSR_NodeId, float, float);
ZCSR_Result zcsr_slider_set_step(ZCSR_UI*, ZCSR_NodeId, float);
ZCSR_Result zcsr_slider_set_value(ZCSR_UI*, ZCSR_NodeId, float);
ZCSR_Result zcsr_slider_get_value(const ZCSR_UI*, ZCSR_NodeId, float*);
ZCSR_Result zcsr_slider_set_orientation(ZCSR_UI*, ZCSR_NodeId, bool);
ZCSR_Result zcsr_slider_set_enabled(ZCSR_UI*, ZCSR_NodeId, bool);
#ifdef __cplusplus
}
#endif
#endif
