#ifndef ZCSR_UI_SEPARATOR_H
#define ZCSR_UI_SEPARATOR_H
#include "zcsr/ui_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZCSR_UI ZCSR_UI;
ZCSR_Result zcsr_separator_set_orientation(ZCSR_UI*, ZCSR_NodeId, bool);
ZCSR_Result zcsr_separator_set_color(ZCSR_UI*, ZCSR_NodeId, ZCSR_Color);
ZCSR_Result zcsr_separator_set_thickness(ZCSR_UI*, ZCSR_NodeId, float);
#ifdef __cplusplus
}
#endif
#endif
