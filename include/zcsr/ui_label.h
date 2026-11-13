#ifndef ZCSR_UI_LABEL_H
#define ZCSR_UI_LABEL_H
#include "zcsr/ui_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZCSR_UI ZCSR_UI;
ZCSR_Result zcsr_label_set_text(ZCSR_UI*, ZCSR_NodeId, const char*, uint32_t);
ZCSR_Result zcsr_label_set_color(ZCSR_UI*, ZCSR_NodeId, ZCSR_Color);
ZCSR_Result zcsr_label_set_alignment(ZCSR_UI*, ZCSR_NodeId, int32_t, int32_t);
#ifdef __cplusplus
}
#endif
#endif
