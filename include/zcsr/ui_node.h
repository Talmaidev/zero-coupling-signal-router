#ifndef ZCSR_UI_NODE_H
#define ZCSR_UI_NODE_H
/* Generic UI runtime — node model. Schema-agnostic: the core never knows concrete
 * settings fields, panel layout or business meaning. First slice uses a FLAT node
 * list with explicit absolute rectangles (free layout); no parent/child tree.
 * No heap: node arrays and storage are caller-owned. Implemented in modules/ui. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t zcsr_ui_id;
typedef uint32_t zcsr_ui_key;

#define ZCSR_UI_ID_NONE  ((zcsr_ui_id)0u)
#define ZCSR_UI_KEY_NONE ((zcsr_ui_key)0u)

typedef struct {
    float x, y, w, h;
} zcsr_ui_rect;

typedef enum {
    ZCSR_UI_VALUE_NONE = 0,
    ZCSR_UI_VALUE_BOOL,
    ZCSR_UI_VALUE_INT,
    ZCSR_UI_VALUE_FLOAT
} zcsr_ui_value_type;

typedef struct {
    zcsr_ui_value_type type;
    union {
        bool  boolean;
        int   integer;
        float real;
    } as;
} zcsr_ui_value;

typedef enum {
    ZCSR_UI_PANEL = 1,     /* solid background rect, non-interactive */
    ZCSR_UI_BUTTON,        /* activation node, no business callback */
    ZCSR_UI_TOGGLE,        /* boolean value */
    ZCSR_UI_SLIDER,        /* int/float value in [minimum, maximum] */
    ZCSR_UI_DROPDOWN,      /* int value selected from options */
    ZCSR_UI_LABEL,         /* non-interactive text */
    ZCSR_UI_SEPARATOR,     /* non-interactive divider */
    ZCSR_UI_IMAGE,         /* textured rect, resolved via resource_key */
    ZCSR_UI_TEXTINPUT,    /* UTF-8 text input, caller-owned buffer */
    ZCSR_UI_SCROLLVIEW    /* scrolling container with clip */
} zcsr_ui_widget_type;

/* One selectable option. label_key is an i18n key; value is the stored int. */
typedef struct {
    zcsr_ui_key label_key;
    int         value;
} zcsr_ui_option;

/* Static description of a node, fully supplied by the assembly layer. */
typedef struct {
    zcsr_ui_id          id;
    zcsr_ui_widget_type type;

    /* Explicit geometry in UI logical (screen) pixels; free layout. */
    zcsr_ui_rect        rect;

    zcsr_ui_key         binding_key;   /* 0 = unbound, does not participate in settings groups */
    zcsr_ui_key         label_key;     /* i18n key for the node label */
    zcsr_ui_key         desc_key;      /* i18n key for the right-side description panel */
    zcsr_ui_key         action_key;    /* semantic action emitted by a button */
    zcsr_ui_key         resource_key;  /* logical image/resource key (IMAGE) */

    zcsr_ui_value       initial;      /* caller-provided reset default */

    float               minimum;
    float               maximum;
    float               step;

    const zcsr_ui_option* options;
    size_t                option_count;

    /* TextInput: caller-owned UTF-8 buffer, NUL-terminated. */
    char*                text_buf;
    size_t               text_cap;

    /* ScrollView: content dimensions in logical pixels. */
    float                content_w;
    float                content_h;

    bool                enabled;
    bool                visible;
    bool                read_only;
} zcsr_ui_node_desc;

/* Runtime node: description plus committed/pending state and interaction flags. */
typedef struct {
    zcsr_ui_node_desc desc;

    zcsr_ui_value committed;   /* confirmed value */
    zcsr_ui_value pending;     /* value being edited */
    zcsr_ui_value initial;     /* reset default */

    bool hovered;
    bool focused;
    bool pressed;
    bool dropdown_open;

    /* TextInput state. */
    size_t caret_pos;      /* byte offset in text_buf */
    size_t scroll_x;      /* ScrollView horizontal offset */
    size_t scroll_y;      /* ScrollView vertical offset */

    uint32_t activation_serial; /* incremented on button activation; polled by host */
} zcsr_ui_node;

/* Value constructors. */
zcsr_ui_value zcsr_ui_value_none(void);
zcsr_ui_value zcsr_ui_value_bool(bool value);
zcsr_ui_value zcsr_ui_value_int(int value);
zcsr_ui_value zcsr_ui_value_float(float value);

/* Strict equality (type must match); floats compare only when both finite. */
bool zcsr_ui_value_equal(zcsr_ui_value a, zcsr_ui_value b);

/* Point-in-rectangle test. */
bool zcsr_ui_rect_contains(zcsr_ui_rect r, float x, float y);

/* True for nodes that hold an editable scalar (participate in dirty/binding). */
bool zcsr_ui_node_is_editable(zcsr_ui_widget_type type);
/* True for nodes that can receive focus and pointer interaction. */
bool zcsr_ui_node_is_interactive(zcsr_ui_widget_type type);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_NODE_H */
