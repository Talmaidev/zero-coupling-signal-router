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

/* 预设机制（旧链）的函数声明已移除：
 *   zcsr_ui_preset_apply / zcsr_ui_preset_match 从来没有实现过，
 *   而且它们签名依赖 ui_panel.h 的 zcsr_ui_context —— 那套运行时同样无实现。
 * 本文件只保留纯数据类型，供需要描述预设表的地方引用。
 * 若将来要真正实现预设，应基于 ui_event.h 那一套（ZCSR_UI*）而不是 zcsr_ui_context。
 */

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_PRESET_H */
