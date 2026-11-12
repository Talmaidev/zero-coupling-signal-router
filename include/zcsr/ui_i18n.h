#ifndef ZCSR_UI_I18N_H
#define ZCSR_UI_I18N_H
/* Generic localization lookup. The core only resolves keys; concrete language
 * tables are supplied by the assembly layer. No translation registry, dynamic
 * loading or callbacks. Implemented in modules/ui. */
#include "zcsr/ui_node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    zcsr_ui_key key;
    const char*  text;
} zcsr_ui_translation;

typedef struct {
    const zcsr_ui_translation* items;
    size_t                     count;
} zcsr_ui_translation_table;

/* Look up `key` in the active table, then the fallback table.
 * Returns NULL when the key is ZCSR_UI_KEY_NONE or appears in neither. */
const char* zcsr_ui_tr(zcsr_ui_key key,
                       const zcsr_ui_translation_table* active,
                       const zcsr_ui_translation_table* fallback);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_I18N_H */
