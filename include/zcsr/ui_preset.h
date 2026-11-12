#ifndef ZCSR_UI_PRESET_H
#define ZCSR_UI_PRESET_H

#include <stddef.h>
#include "zcsr/ui_node.h"   /* zcsr_ui_key / zcsr_ui_rect */
/* Generic preset mechanism. A preset is pure data: named binding_key -> value
 * pairs. Concrete preset tables live in the assembly layer; this file only
 * implements the generic rules. No engine calls, no side effects beyond pending.
 * Implemented in modules/ui. */
#include "zcsr/ui_panel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    zcsr_ui_key   binding_key;
    zcsr_ui_value value;
} zcsr_ui_preset_entry;

typedef struct {
    zcsr_ui_key                 name_key;
    const zcsr_ui_preset_entry* entries;
    size_t                      entry_count;
} zcsr_ui_preset;

/* Validate every entry (bound editable node exists, type/range accepted) and
 * only then write all values into pending. Does not touch committed. */
zcsr_ui_status zcsr_ui_preset_apply(zcsr_ui_context* ctx,
                                    const zcsr_ui_preset* preset);

/* Compare presets against the current pending values. Returns the unique
 * matching preset index, or -1 when none match or more than one matches
 * (the -1 case is presented as "custom"). */
int zcsr_ui_preset_match(const zcsr_ui_preset* presets, size_t preset_count,
                         const zcsr_ui_context* ctx);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_PRESET_H */
