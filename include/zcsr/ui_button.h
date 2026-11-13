#ifndef ZCSR_UI_BUTTON_H
#define ZCSR_UI_BUTTON_H
#include "zcsr/ui_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZCSR_UI ZCSR_UI;
ZCSR_Result zcsr_button_set_text(ZCSR_UI*, ZCSR_NodeId, const char*, uint32_t);
/* zcsr_button_set_colors 已删除：四态配色属 UIM 组件属性域，引擎无承载模型。 */
ZCSR_Result zcsr_button_set_enabled(ZCSR_UI*, ZCSR_NodeId, bool);
#ifdef __cplusplus
}
#endif
#endif
