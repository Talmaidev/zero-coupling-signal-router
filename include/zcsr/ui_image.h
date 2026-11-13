#ifndef ZCSR_UI_IMAGE_H
#define ZCSR_UI_IMAGE_H
#include "zcsr/ui_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZCSR_UI ZCSR_UI;
ZCSR_Result zcsr_image_set_resource(ZCSR_UI*, ZCSR_NodeId, uint32_t);
ZCSR_Result zcsr_image_set_uv(ZCSR_UI*, ZCSR_NodeId, const ZCSR_Rect*);
ZCSR_Result zcsr_image_set_tint(ZCSR_UI*, ZCSR_NodeId, ZCSR_Color);
ZCSR_Result zcsr_image_set_fit_mode(ZCSR_UI*, ZCSR_NodeId, int32_t);
#ifdef __cplusplus
}
#endif
#endif
