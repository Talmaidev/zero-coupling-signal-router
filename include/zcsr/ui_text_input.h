#ifndef ZCSR_UI_TEXT_INPUT_H
#define ZCSR_UI_TEXT_INPUT_H
#include "zcsr/ui_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZCSR_UI ZCSR_UI;
ZCSR_Result zcsr_text_input_set_text(ZCSR_UI*, ZCSR_NodeId, const char*, uint32_t);
ZCSR_Result zcsr_text_input_get_text(const ZCSR_UI*, ZCSR_NodeId, char*, uint32_t, uint32_t*);
ZCSR_Result zcsr_text_input_set_max_bytes(ZCSR_UI*, ZCSR_NodeId, uint32_t);
ZCSR_Result zcsr_text_input_set_read_only(ZCSR_UI*, ZCSR_NodeId, bool);
ZCSR_Result zcsr_text_input_set_placeholder(ZCSR_UI*, ZCSR_NodeId, const char*, uint32_t);
ZCSR_Result zcsr_text_input_set_cursor(ZCSR_UI*, ZCSR_NodeId, uint32_t);
ZCSR_Result zcsr_text_input_get_cursor(const ZCSR_UI*, ZCSR_NodeId, uint32_t*);
ZCSR_Result zcsr_text_input_set_selection(ZCSR_UI*, ZCSR_NodeId, uint32_t, uint32_t);
ZCSR_Result zcsr_text_input_set_enabled(ZCSR_UI*, ZCSR_NodeId, bool);
#ifdef __cplusplus
}
#endif
#endif
