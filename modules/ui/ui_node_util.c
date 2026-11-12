/* ui_node_util.c — 节点模型的值构造 / 比较 / 命中 / 类型判定工具。
 *
 * 这 7 个函数在 ui_node.h 里声明了很久，但仓库里一直没有实现（典型的
 * "有声明无实现"）。补上是因为 v2 的 ui.c 与宿主都要用它们，而且它们
 * 是纯函数、零副作用，没有依赖旧链（ui_panel.h / zcsr_ui_context）。
 *
 * 规则严守头文件承诺，不做任何"顺手放宽"：
 *  - value_equal 要求 type 完全一致；浮点只在两边都有限时才比较，NaN 一律不等。
 *  - rect_contains 用半开区间 [x, x+w) / [y, y+h)；宽或高为非正数时恒 false。
 */
#include <math.h>

#include "zcsr/ui_node.h"

zcsr_ui_value zcsr_ui_value_none(void)
{
    zcsr_ui_value v;
    v.type = ZCSR_UI_VALUE_NONE;
    v.as.integer = 0;
    return v;
}

zcsr_ui_value zcsr_ui_value_bool(bool value)
{
    zcsr_ui_value v;
    v.type = ZCSR_UI_VALUE_BOOL;
    v.as.boolean = value;
    return v;
}

zcsr_ui_value zcsr_ui_value_int(int value)
{
    zcsr_ui_value v;
    v.type = ZCSR_UI_VALUE_INT;
    v.as.integer = value;
    return v;
}

zcsr_ui_value zcsr_ui_value_float(float value)
{
    zcsr_ui_value v;
    v.type = ZCSR_UI_VALUE_FLOAT;
    v.as.real = value;
    return v;
}

bool zcsr_ui_value_equal(zcsr_ui_value a, zcsr_ui_value b)
{
    if (a.type != b.type)
        return false;

    switch (a.type) {
    case ZCSR_UI_VALUE_NONE:
        return true;
    case ZCSR_UI_VALUE_BOOL:
        return a.as.boolean == b.as.boolean;
    case ZCSR_UI_VALUE_INT:
        return a.as.integer == b.as.integer;
    case ZCSR_UI_VALUE_FLOAT:
        /* 只有两边都是有限值才比较；NaN/Inf 一律视为不相等，避免"脏值"被
         * 判定成"无改动"。 */
        if (!isfinite(a.as.real) || !isfinite(b.as.real))
            return false;
        return a.as.real == b.as.real;
    default:
        return false;
    }
}

bool zcsr_ui_rect_contains(zcsr_ui_rect r, float x, float y)
{
    if (r.w <= 0.0f || r.h <= 0.0f)
        return false;
    return (x >= r.x) && (x < r.x + r.w) && (y >= r.y) && (y < r.y + r.h);
}

bool zcsr_ui_node_is_editable(zcsr_ui_widget_type type)
{
    switch (type) {
    case ZCSR_UI_TOGGLE:
    case ZCSR_UI_SLIDER:
    case ZCSR_UI_DROPDOWN:
    case ZCSR_UI_TEXTINPUT:
        return true;
    default:
        return false;
    }
}

bool zcsr_ui_node_is_interactive(zcsr_ui_widget_type type)
{
    switch (type) {
    case ZCSR_UI_BUTTON:
    case ZCSR_UI_TOGGLE:
    case ZCSR_UI_SLIDER:
    case ZCSR_UI_DROPDOWN:
    case ZCSR_UI_TEXTINPUT:
    case ZCSR_UI_SCROLLVIEW:
        return true;
    default:
        return false;
    }
}
