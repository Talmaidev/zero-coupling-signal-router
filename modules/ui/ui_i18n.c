/* ui_i18n.c — 翻译查表。
 *
 * zcsr_ui_tr 在 ui_i18n.h 里声明了，但仓库里从来没有实现过：头文件写着
 * "Implemented in modules/ui"，而 modules/ui 下并没有对应的 .c。这里补上，
 * 语义严格按头注释：先查 active，再查 fallback，都查不到或 key 为
 * ZCSR_UI_KEY_NONE 时返回 NULL（调用方负责显示原 key 或空串）。
 *
 * 不做 hash、不做缓存：表是宿主提供的静态数组，量级在几百条，线性扫描足够，
 * 也避免引入堆分配。
 */
#include <stddef.h>

#include "zcsr/ui_i18n.h"

static const char* lookup(const zcsr_ui_translation_table* table, zcsr_ui_key key)
{
    if (table == NULL || table->items == NULL)
        return NULL;

    for (size_t i = 0; i < table->count; ++i) {
        if (table->items[i].key == key)
            return table->items[i].text;
    }
    return NULL;
}

const char* zcsr_ui_tr(zcsr_ui_key key,
                       const zcsr_ui_translation_table* active,
                       const zcsr_ui_translation_table* fallback)
{
    if (key == ZCSR_UI_KEY_NONE)
        return NULL;

    const char* text = lookup(active, key);
    if (text != NULL)
        return text;

    return lookup(fallback, key);
}
